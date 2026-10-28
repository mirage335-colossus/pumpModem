# Validation status

Updated 28 October 2026. This page records current qualification, release status,
coverage limits and useful historical anchors. It is not a session log. Replace
superseded status here; keep one latest [resource snapshot](ci-resource-results.json)
and use Git history for comparisons. Detailed studies remain in the
[evidence index](indexes/evidence.md).

## Current two-tone Helper Carrier candidate

The current human checkpoint is `572b051`, with initial-descriptor support
source `9977084b…`/test `8fd185fa…`, unchanged core `2c118bf0…` and Live
`70a546c1…`. Current GUI `79d40073…` passes the EME Planner caution check and
headless self-check. The prior `64824037…` GUI retains its standard build and
five focused passes. The retained HF15k capture restores the collision owner but is **STRICTFAIL**: four
premature native ENDs among 17 identities despite fixture exit 0/canonical
exact eight bits. The private calendar-END proposal is held, unbuilt and
unintegrated after bounded review. The older `2101/3b7` END-consistent capture left this collision unexercised.
Per-ID public Snapshot lifetimes remain unavailable. The preceding `0463/276e`
binding retains History/pending-GUI, raw/provenance/actual-RF, full GUI 39/39 and
six selected ordinary-Live passes, including unskipped exact-source coverage;
these are not full reruns on `2c118`.
Strict Rate-360 `21bf/913` and full HF40/fully GPSDO-disciplined HF `85a5/d8e3`
passes retain their own bindings; those full runs have not been repeated on this app.
Measured HF40 reference quality is accepted as useful for that six-physical-core/
2 GiB simulator; estimator tuning remains frozen rather than chasing GPSDO
specifications or a CRLB target.
The estimator-tuning freeze and short architecture discriminators
are in the [operating guide](helper-carrier.md#receiver-architecture-decision).
The completed contract binds the `65ee248` implementation/core `341b2b35…`;
later focused builds have separate bindings. Documentation
updates do not establish new runtime qualification. Earlier receipts retain
their own source/configure-version bindings. Final qualification remains
pending. The preceding checkpoints `871a55a`, `aef6580`, `62db765` and `8355e64` and their
source-bound evidence remain distinct from the superseded single-tone R16 source/binary.
R17 measured retune-prefix FFT work falls with all LF alternatives retained,
but total CPU rises. Earlier HF40 deadline failures remain preserved separately
from the current pass. An earlier HF15k primary pass
retains a strict secondary-row failure, and full EME fails. The rebuilt GUI is
available; bounded probes now narrow the failed-decision mechanisms, while
controlled diagnosis and full qualification remain open.
Each result retains its own build binding; full receiver/contract qualification
remains incomplete. The application now has
90% pure pilot/10% auxiliary power, High-only adjacent placement, one shaped
auxiliary reversal centered at 240 ms, and auxiliary-only optional metadata.
Ordinary Robust power targets, Rate, wire/framing and keyed timing remain the
contract. Native composite headroom changes the audio calibration: at unchanged
volume the data gain is about 1.353 dB lower at 40 W/4 W and 1.989 dB lower at
15 kW/4 W than the old single-tone format. Physical RF output needs recalibration;
sampled headroom/oversampling compensation scales signal and noise together.
This retains Interleaved V2's separate **−6.0206 dB sampled DATA gain** relative
to nominal RF budget; it does not erase that difference or change path loss.
Total Helper and pilot C/N0 are separately accounted.

The initial rebuilt GUI is `build/dev/datapump-gui`. The actual commands were:

```sh
./build.sh --build-jobs 6 >local-evidence/helper-two-tone/gui-build-initial.log 2>&1
CCACHE_DIR="$PWD/build/ccache" ./build.sh --build-jobs 6 >local-evidence/helper-two-tone/gui-build.log 2>&1
ctest --test-dir build/dev --output-on-failure -j 1 -R '^(helper_carrier|helper_filter_response|helper_frontend|helper_planner_forecast|gui_launch_command)$' >local-evidence/helper-two-tone/focused-initial.log 2>&1
```

The [first application build](../local-evidence/helper-two-tone/gui-build-initial.log)
failed during compilation with `ccache: error: Read-only file system`. The
[workspace-cache build](../local-evidence/helper-two-tone/gui-build.log) passed.
The [initial five checks](../local-evidence/helper-two-tone/focused-initial.log)
passed 5/5 in 5.17 seconds: carrier, filter response, frontend, Planner forecast
and GUI launch command. A later [focused-target build](../local-evidence/helper-two-tone/focused-build.log)
failed because `test_gui_link_planner.cpp` lacked the include declaring
`transfer::receiver_utc_rate_correction`; that test setup was repaired. The
[second target build](../local-evidence/helper-two-tone/focused-build-r2.log)
completed linking the selected targets. That build alone is not a runtime test
pass. Separate [tracker](../local-evidence/helper-two-tone/tracker-initial.log)
and [helper-control](../local-evidence/helper-two-tone/gui-helper-controls.log)
checks passed. Later source changes require their own focused results and final
GUI/source hashes; the initial checkpoint does not bind those evolving inputs.

### Current measured status and remaining failures

The [Planner caution build/checks](../local-evidence/helper-two-tone/architecture-review-r1/planner-caution-r1/results.json)
pass: standard build 8.323048 CPU seconds, target build 13.684663,
`gui_link_planner` 63.336637 CPU/71.120531 wall seconds, and GUI self-check
6.846337 CPU/6.974529 wall seconds. [Launch/source bindings](../local-evidence/helper-two-tone/architecture-review-r1/planner-caution-r1/launch.json)
retain page `f2e0436e…`/test `b9fd3c27…`; output GUI is `79d40073…`, with core
`2c118bf0…` unchanged. Pins are unchanged and all processes are reaped. The
specific caution is tested at 220/900-pixel widths; this diagnostic is built
and tested, without changing the strict HF15k or private EME failure outcomes.

The [current initial-descriptor main checks](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/main-checks-r1/results.json)
pass with unchanged pins/all processes reaped: standard build, target build,
five focused tests (40.627300 CPU seconds) and GUI self-check (6.817171).
[Launch/source bindings](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/main-checks-r1/launch.json)
retain header `9977084b…`, test `8fd185fa…`, Live `70a546c1…`, core `2c118bf0…`
and GUI `64824037…`. No estimator tuning changed.

The [original full HF15k fixture](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/results.json)
exits 0 but is **STRICTFAIL**. It captures all 1,188,236 samples, with 11,711 FFT
jobs/306 transforms/207 owners, 152.656895 external CPU/41.049254 wall seconds.
[Actual launch and pins](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/launch.json)
retain the maintained +3 kHz/+100 ppm shared-radio profile. The corrected
[17-identity audit](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/audit-r2.json)
(`ee6f7088…`) retains **four premature native ENDs**. Owner 39 now executes
56 FFT jobs, with 77/77 whole-score queries contained, instead of zero work.
Its alias next address 74 fails while canonical address 75 scores 110.90 in the
same window and TX DATA continues. Exact canonical eight bits and monotone
intended commits do not waive this physical-END failure. The sealed trace has
zero drops, but per-ID public lifetimes remain unavailable. Three qualified
poll escapes represent two distinct dates; they remain engineering coverage
failures, not independent confidence trials. Bounded Astra review closes private
calendar-END proposal `038e1a30…` as **held, unbuilt and unintegrated**:
`confirmed_positive` includes accumulated weak chains, so exact prefixes/date
cones cannot manufacture historical standalone positives. Candidate quotas can
return before the content callback, and zero-bit retry must occur after append
at the next symbol. A minimal two-pass Bank publication/actual-receipt join,
separately funded bounded tokens and retirement protection remain unresolved.
No END repair pass or matched-PCM CPU-gain claim follows. The earlier audit metadata error and its correction
remain retained together, without another DSP run. The specific EME Planner
caution is subsequently built/tested as recorded above.

The [new strict fixture target build](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/newstrict-fixture-r1/main-check-r1/results.json)
passes using 14.357223 CPU seconds, source `427f9b7b…`/binary `ae65dc37…`.
Retained-trace reinspection then exits 1 in 0.378098 CPU seconds with requested/
available audit: 17 native completions/17 failed receipts, 13 quiet controls,
four conflicts and zero unresolved. [Actual commands/bindings](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/newstrict-fixture-r1/main-check-r1/launch.json)
retain GUI `79d40073…`/core `2c118bf0…`; pins are unchanged/all processes reaped.
Owners 25, 39, 53 and 41 join actual same-window standalone/full-suffix DATA
addresses/bits and TX end. Missing evidence/probe-off states remain explicitly
unavailable. Online fixture success/invalid now includes this guard, but there
is **no new Receiver, PCM or Session run**, no END repair and no new acquisition
qualification.

The [original matched-inspector assumption](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/newstrict-fixture-r1/main-check-r1/matched-results.json)
**fails**: it expected old exit 0, but the old posthoc inspector also exits 1
(0.342272 CPU seconds), with the same existing summary. Reinspection supplies
`ordinary_baseline=nullptr`, so an independent no-omission conditional-birth
check cannot qualify the primary. The [corrected comparison scope](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/full-r1/newstrict-fixture-r1/main-check-r1/comparison-r2.json)
verifies only the new four-witness diagnostic/13 quiet controls; this is not an
old-pass/new-fail acquisition comparison. The original full Live fixture exit
0 remains separate, and the independent inspector limitation is unfixed.

The [preceding `2101` HF15k containment run](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/full-r1/results.json)
passes the fixture, exit 0, with final public exact `11010110`: 1,188,236 samples,
6,412 FFT jobs/145 transforms/207 owners, 712,631,312 peak DSP bytes, 92.415521
external CPU/30.661176 wall seconds, all processes reaped and pins unchanged.
[Launch/bindings](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/full-r1/launch.json)
retain the same maintained +3 kHz/+100 ppm shared-radio profile, six physical
cores/2 GiB, core `2101c0c9…`/GUI `3b7ed788…`, fixture `50ece258…`, channel
`e0b3ed7e…` and observation fixture source `4da37fd1…`. The [audit](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/full-r1/audit.json)
finds canonical owner 9 now executes 266 FFT jobs/four transforms, with 140/140
whole-score queries valid and contained, versus zero FFT work on `0463`. These
separate captures provide no matched-PCM CPU-gain claim.

The sealed 53,792-line trace has zero drops. TX-owned valid DATA maps to sample
721,767.83094; a conservative finite-support envelope ends at 722,404. All five
native failed-next-address-117 windows end at 808,612–808,630, at least 14.086
seconds after that bound. This establishes END consistency in this capture,
not general old-collision repair. Four intended commit IDs/twelve records are
monotone, and the final public Snapshot has the exact row; all per-ID public
Snapshot lifetimes were not retained. Epoch-31 collision owner 39 executes zero
FFT jobs, with 155/170 receipt lockouts, so historical wrong-address END remains
**unexercised**. [Read-only owner-39 analysis](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/full-r1/owner39-readonly-r2.json)
finds actual produced block U ≤0.0233486 Hz throughout its support, inside birth
radius 0.0298115 Hz. An optional one-second posterior widens to 0.0338673 Hz;
mandatory sixteen-bucket coarsening includes startup U 0.0888925 Hz. A proposed
immutable initial-descriptor fallback/persisted disjoint-support walk has no
history/storage growth. Its [reviewed source](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/descriptor-initial-r1/source-binding.json)
`9977084b…`/test `8fd185fa…` was subsequently built and passed focused checks;
the separately bound full run above exposes the strict END failure.
Bounded review also finds missing full-prefix/negative-proof API support for an
END-ambiguity component; that component is setup-blocked, not passed.

The [five containment checks](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/main-focused-r1/results.json)
pass on their preceding `e5b6/c5be` binding. The [observation rebuild/simulation](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/observation-build-r1/results.json)
binds `2101/3b7`; `helper_sim` passes in 2.686303 CPU seconds. The
[that GUI self-check](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/containment-r1/current-gui-self-check-r1/results.json)
passes in 6.944563 CPU seconds. Full GUI/Live results below retain `0463` bindings.

The [paired actual-History validation](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/validation-results.json)
retains old failure/new pass for the pending-ID replacement. Main source
`12cff263…` makes physical-birth separation independent of Helper, without END,
scoring or layout changes. ±chip timing refinements, different-duration and
nonuniform timelines remain supported; the 64-entry cap applies to both modes.
The [main build](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/main-build-r1/results.json)
passes using 95.735690 CPU seconds, binding core `0463aa14…`/GUI `276e70e2…`.
The [first focused run](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/main-focused-r1/results.json)
retains one stale ordinary-alias expectation failure; raw/provenance/actual-RF
DATA checks pass on this exact core. After a reviewed two-line expectation-only
repair, [R2 checks](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/main-focused-r2/results.json)
pass: History 0.034022 CPU seconds, GUI pending batch 0.676285 and GUI self-check
8.816532, after 8.628461 test-only relink CPU seconds. [Inputs](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/main-focused-r2/inputs.json)
bind current test `1a025fcb…`; all processes are reaped. These bounded History
checks do not establish native END or a new full RF-only/Rate-360/HF40 run.
The [full GUI stage](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/gui-stage-r1/results.json)
passes **39/39**, using `./build.sh test gui --build-jobs 2 --jobs 1` under
affinity 0–11: 236.586954 CPU/400.415282 wall seconds, all processes reaped and
core `0463aa14…`/GUI `276e70e2…` pins unchanged. The failed 299-case contract
below remains distinct; no platform/display/audio or hardware coverage follows.
The [six-case ordinary-Live selection](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/newpending-prefix-r1/live-stage-r1/results.json)
passes **6/6**: `live`, profiles, resources, transmit-lock, replay staging and
`live_exact_source` (passed, not skipped). It uses 261.415944 test CPU/173.261488
wall seconds plus 16.408227 build CPU seconds, all processes reaped, with core
`0463aa14…`/GUI `276e70e2…` pins unchanged. This selected coverage does not rerun
the full 299-case contract or qualify other payload/platform configurations.

The [preceding `0463` HF15k run](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/results.json)
finishes **FAILED**, exit 1 without a cap: full capture 1,188,236 samples, zero
primary bits/END, 3,878 FFT jobs/111 transforms/207 owners, 65.401327 external
CPU/27.549556 wall seconds, all processes reaped and pins unchanged.
[Commands](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/commands.json)
retain crystal/crystal shared radio, actual +3 kHz/+100 ppm, 15,000 W Helper/4 W
DATA, 30 MHz Shift, Rate 36/interleave 10, path loss 180 dB, seed 5181, Default
clock/0 ms configured audio error, 2 GiB and six physical cores.
`--receiver-probe` is observation-only. The [fixture binding](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/fixture-binding.json)
is source `98041e95…`/binary `86e5ab58…` on core `0463aa14…`.

Independent source/trace diagnosis confirms the clock sign and finds canonical
epoch 1,800,000,004/owner 9 executes **zero FFT jobs**. Valid mandatory block
uncertainty is at most ±0.088892536958 Hz, inside constructor radius
0.097681599458 Hz. Optional historical refinement instead widens the hull to
[−0.887628370, +0.846202091] Hz and overrides the base, causing containment
lockout. The [full trace](../local-evidence/helper-two-tone/hf-crystal15k-current-0463-r1/full/hf-crystal-on-bits-61534043810390.jsonl)
contains two suffix rows/four commits: zero primary bits does not mean no
accepted bits globally. This source-bound failure remains failed. Its later
containment repair/pass is separately bound above, with rejection/reset behavior
preserved; this failure does not establish a physical precision limit.

This failure is distinct from the old R17 core `adb6234d…`/fixture `ad61…`
(GUI sibling `8b68…`) that printed `decoded_exact` while its independent audit
failed alias end 460,517 before true payload end 721,595. R19 core `091bb…`/
fixture `c7b8…` probe exits 2 and confirms wrong address 74 versus strong 75.
That false-END defect remains unresolved. A fixture's six-second gap does not
establish actual-source absence; original source-bound failures remain.

The [raw-frequency main build](../local-evidence/helper-two-tone/rate360-frequency-admission-r1/main-build-r1/terminal.json)
and targets pass with unchanged source guards, 93.052755 aggregate CPU seconds.
The [frozen `21bf/913` focused terminal](../local-evidence/helper-two-tone/rate360-frequency-admission-r1/main-focused-r1/terminal.json)
is all-pass/reaped/guarded: strict supplied Rate-360 Fake-FHSS retains all eight
source/full-wire bits, public pending **one row**, scored END and replay over
355,201 samples/24.666736 audio seconds. It executes 28,998 FFT jobs/537 transforms/
40 owners, 553,372,420 peak DSP bytes within 2 GiB, in 58.419223 external CPU/
14.884920 wall seconds on six physical cores. [Actual commands/bindings](../local-evidence/helper-two-tone/rate360-frequency-admission-r1/main-focused-r1/inputs.json)
retain Default/70 ms, 1,500 W Helper and fixed physical RF; this is Fake-FHSS,
not hardware RF retuning. The original zero-owner failure below remains failed.
The new mandatory contract test `helper_raw_frequency` and provenance/actual
changing-RF independent-hop-phase/loss DATA checks pass, as does 64-bit normal
loss (9.845437 CPU/68.080925 wall seconds). GUI self-check passes in 7.754300 CPU/
6.790075 wall seconds. These are selected checks, not a full contract rerun.

The repair uses the existing prepaid 400 ms FIFO and provider stamps for dated
raw M support, including full FIR dependencies/−512 real-delay coordinates.
It retains all nine unresolved LF choices, exact `q=0` and no phase authority,
without estimator, split, lead or threshold changes. The
[5.505507-CPU-second component](../local-evidence/helper-two-tone/rate360-frequency-admission-r1/component-r1/results.json)
is geometry-only. The [completed folded-job/date audit](../local-evidence/helper-two-tone/rate360-frequency-admission-r1/main-focused-r1/folded-raw-frequency-audit.json)
matches all 2,803 scientific frequency observations to the old zero-owner
failure; only raw-stage retention changes. Its 2,726 qualified polls have maximum
diagnostic error 0.0110401 Hz/zero exclusions, but are correlated, not calibrated
confidence trials. All 224 input descriptors have exact `q=0`/no phase correction;
491 whole-score queries and every one of 28,998 FFT jobs join stamped current
dated M support with all nine LF alternatives. The first M certificate at
sequence 107 precedes optional 108,368-byte reclamation at 108/constructor 110;
mandatory provenance survives. ID 2/revision 1 progresses `110`→`11010`→`1101011`→
`11010110`, completing at feed 247,077 after payload end 159,432 with 87,645
fully scored absent samples. Capture 355,201 and ordinary frontier 349,201 remain
distinct. Raw staging is 23,040 samples at 57,600 Hz = 400 ms/92,160 bytes.
One invalid checkpoint join/eight unfinished reads prevent zero-backlog or
hardware real-time claims. Coarse native CPU is 55.828286 seconds: search
48.225582, control 7.319191, rolling 0.255678, publication 0.021394 and setup
0.006442. Fine FFT/Helper/frontend costs are unpartitioned, not zero. The normal
branch has focused coverage; older full-HF40 receipts are separate, without
another heavy repetition.

The [preceding additive build](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/build/terminal.json)
passes in 144.180073 aggregate CPU seconds, binding core `85a53d89…`/GUI
`d8e3be8d…`, accepted headers `20e5ff86…`/`1167dca8…` and regression `4fdaafbd…`.
[Focused checks](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/focused/terminal.json)
all pass with unchanged guards/all processes reaped: direct independent actual
RF/per-hop-phase reception keeps exact `001`, ID 2 and scored END, 6,195 FFT
jobs/42 transforms/13 owners in 2.440608 external CPU seconds. The four
modes/provenance, 64-bit normal-loss and GUI self-check also pass.
The [independent trace audit](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/focused/additive-trace-audit.json)
shows the first four owners now execute 1,407/1,407/1,407/1,365 FFT jobs instead
of zero each; outside-constructor pauses fall 252→0. Measured U stays
1.114021028934 Hz while constructor bounds admit 45 pairs/all three LF rows.
Every one of the 6,195 jobs joins dated current-witness support. This restores
required work, without a CPU-saving or improved-precision claim.

The [preceding additive full-HF40 terminal](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/qualification-hf40/terminal.json)
passes/reaps with all source/binary/key guards unchanged. It retains exact eight
source/full-wire bits, public pending identity, scored END and replay over
1,193,132 samples/194.9562 audio seconds, 208 owners, 103,663 FFT jobs/82 transforms,
720,167,794 peak DSP bytes within 2 GiB, and 687,268 KiB max RSS. External CPU is
869.479448 seconds, wall 114.150555 seconds on six physical cores (about 4.46 CPU
seconds/audio second). [Actual inputs](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/qualification-hf40/inputs.json)
retain the same supplied settings and lab key; assistance is frequency-only
(`ever_frequency=1`, phase/timing zero), without added lead or SNR changes.
Cost increases over the preceding pass's 66,192 jobs/39 transforms and
562.536933 CPU seconds; restored scoring/larger constructors are not savings.
Both runtime-CPU observers
have one invalid checkpoint join/seven unfinished reads; current uncommitted
final read is one versus prior zero. Total external CPU remains valid, but
complete block-latency/queue-backlog qualification does not follow. This is
practical acceptance for the stated simulator profile, not hardware real-time.

The [independent full-HF40 cost audit](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/focused/full-HF40-cost-audit.json)
finds the same 208 owner keys and semantically identical raw references.
Executing owners increase 18→49. Of **37,471 extra FFT jobs (+56.61%)**, 35,371
(94.40%) restore 31 formerly zero-work owners and 2,100 widen existing dynamic
masks. The 43 added transforms split 42 restored/one existing; external CPU
rises 54.56%. Peak eleven scorer slots describe allocated workspaces, not
measured concurrent utilization. `receiver_component_cpu` was filtered out of
both traces, so thread/component CPU cannot be reconstructed; a future fixture
filter edit recovers no historical counters. Primary ID 3/revision 2 exact
`11010110` and secondary ID 2/revision 1 suffix `1010110` are identical in both
runs, complete at physical end 727,789/feed frontier 858,958. The secondary stays
separate; this maintained pass is not single-row/suffix-free strict cable
qualification. This source-bound cost increase is not a general cheap-receiver
or total-work-saving claim.

The full-HF40 [trace](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/qualification-hf40/full/hf-on-bits-52547576982689.jsonl)
uses **legacy M**, not typed DATA marginalization. Constructor sequence 106 has
H 2462.5 Hz, D 1500 Hz, measured U 0.3890134488845302 Hz, radius
0.3978025113845302 Hz and 467 pairs. Mask sequence 161 retains nine original/nine
selected LF alternatives, not 55. Its existing `D−H` shear preserves pilot/audio
correlation. The actual-RF typed DATA path conservatively marginalizes the full
joint covariance to scalar `fD`/clock coefficient zero, losing conditional
correlation. That is a separate unquantified optimization gap, not a bound
correctness failure, physical floor or cause of this HF40 cost. A possible dated
affine parameter could reuse the selector; none is implemented or measured.

The [preceding `85a5` GPSDO/Fake-FHSS controls](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/fhss-gps-controls/terminal.json)
are reaped with unchanged source/binary/key guards. Fully disciplined RF **and
audio** HF control passes exact eight bits/END with Default/70 ms: 63.283963 CPU/
16.188821 wall seconds, 5,103 FFT jobs/243 transforms/176 owners and 996,080
samples. It is distinct from the RF-only control below, without a matched helper
CPU ratio. That Rate-360 Fake-FHSS case **fails** after 355,201 samples/
24.667 audio seconds, 1.416671 CPU seconds, zero owners/bits/END despite qualified
pilot U ≈0.0157 Hz. Bounded diagnosis identifies folded raw `q=0`/provenance
admission lockout; no physical precision, phase or C/N0 cause is established.
The current repair/pass above has a different binding; this failure and older
Rate-360 passes remain retained. [Actual commands and scope](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r3-additive/fhss-gps-controls/inputs.json)
preserve the fixed-physical-RF Fake-FHSS distinction. The separate private EME
closure has no acceptance result. Its
[47-member closure compile](../local-evidence/helper-two-tone/eme-shared-gate-r1/closure-r1/compile-execution.json)
passes in 112.514430 CPU seconds with the public ABI frozen to `85a5`. The
[first numeric control](../local-evidence/helper-two-tone/eme-shared-gate-r1/closure-r1/validation-oracle-coordinate-failed/controls-on/run.json)
fails in 1.193671 CPU seconds on the scalar absolute-carrier-phase oracle. An
authorized one-line oracle-coordinate repair leaves the core unchanged. The
[final bounded result](../local-evidence/helper-two-tone/eme-shared-gate-r1/closure-r1/bounded-first-score-cost-result.json)
is **private COSTFAIL**: total compile 117.579083/150 CPU seconds, numeric controls
5.821313/10 including both preserved failures, total runtime 273.081667 seconds;
[final bindings](../local-evidence/helper-two-tone/eme-shared-gate-r1/closure-r1/final-bindings.json)
retain all 47 members/ABI and reaped processes. OFF finishes its fixed fence in
77.181022 CPU seconds; its best full symbol-index-0/lane-7 score
30.1203018283641 is below **31.7153153422971** in this callback scope. The older
31.0201 threshold remains a separate source-bound result. Ordinary cache use is
active on all 85,253 pushes, with 1,886,843 hits/11,982,303 misses. ON reaches its
190-CPU cap (190.079332 CPU/189.184817 wall seconds, exit −9) before canonical
first score, fixed fence and public-input hash. There is no ON score gain or
whole-fence cost ratio.

OFF/ON corrected-input equality is not established: first selection is 455
versus 437 pairs, start offsets 15.641/15.627 seconds, admission 30,856/29,656,
and initial captured ends 1,581/2,048. A read-only source explanation involving
start/async TX-ready timing and partial-read flush partitioning is plausible,
not sole-cause proof. Bounded Astra review keeps the implementation private.
The later [ordinary-cell/gate-deficit component](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-component-r1/analysis.json)
passes geometry/Fit/scalar/quarter/`ln(2)`/split checks at 1e−8 tolerance in three
combined geometries. In one fixed-order comparison, optional CPU changes
0.681269→0.382669 seconds; ordinary-base CPU 0.124015 seconds is separate. All N
samples are classified, while 32.65864% need nonunity row reads. This proves
neither Live cost nor an isolated score gain. [Final bindings](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-component-r1/final-bindings.json)
retain all reaped runs and cumulative 121.301076 compile/275.907174 runtime CPU
seconds including original failures, without a reset. The later private
`deficit-live-r1` integration is now **COSTFAIL**, not a main change or promotion.
Its [compile](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/compile-execution.json)
uses 178.601767 CPU seconds; [six controls](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/controls/execution.json)
pass using 5.523147. Archive `9478fde5…`/binary `49680bf7…` OFF completes in
80.967815 CPU seconds, with first score 30.1203 below 31.7153. The original
[serial-1 parser failure](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/parser-serial1-failed/OFF/guards.json)
is retained; a one-line serial-2 parser repair and [read-only guard](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/OFF/guards-serial2.json)
pass without rerunning OFF. [ON](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/ON/run.json)
hits its cap at 190.066988 CPU seconds before a canonical score, sealed footer,
fixed fence or raw/read/scorer binding. Its last joined sample is 695,304 from
owner origin 83,304. [Partial-scope accounting](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/ON/partial-scope-run.json)
ends at 278.141803/280 new-stage runtime CPU seconds, 554.048977 cumulative,
all processes reaped. No matched score gain or CPU ratio follows; earlier gate
cost failures and partial mismatches remain preserved. Read-only auxiliary
review of [OFF stdout](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/OFF/stdout.log),
[trace](../local-evidence/helper-two-tone/eme-shared-gate-r1/deficit-live-r1/OFF/eme-on-bits-63100411869505.jsonl)
and guarded inputs separates configured straight-tone C/N0 before waveform gains—total
30.7609/pilot 30.3033/auxiliary 20.7609 dB-Hz—from OFF measured pilot median
30.0119 dB-Hz (lead through 20 seconds: 30.0025). No independent measured
auxiliary C/N0 is available. OFF has 495 pilot phase updates but no qualified
clock over 8,215 raw publications. Actual optional auxiliary work uses 1.56155
CPU seconds/175.0527 audio seconds (0.00892 CPU/audio), distinct from whole OFF
fixture CPU 80.85284/external 80.967815.

Current markers feed only the clock owner, not a qualified timing Reference.
The [pulse-lattice backend](../src/pattern_correlator.cpp) declines the compact timing-prior API, and even a
perfect 400 ms periodic prior intersects every one-second origin cell before
padding. Stronger markers alone therefore cannot remove the observed 46
arrival alternatives. This is an implementation/alias-geometry limit, not a
physical timing/noise bound. The earlier private punctuation estimator's
0.282916 CPU/audio and 75/81 detections could be worthwhile if measured receiver
work were removed; it has no current selector authority, so neither intrinsic
impracticality nor a need for weaker-tone acquisition follows. The proposed
RF-only fair-conditioning component is **unbuilt/setup-blocked**: whole-symbol support 87,040
exceeds its 32,768-sample export, with unresolved 512-sample/filter-date setup
gaps; it supplies no matched control.

The [preceding supplied HF40 qualification](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/qualification-hf40-r1/results.json)
passes (exit 0): all eight source/full-wire bits, public pending identity,
prefixes, fully scored END and replay. It captures 194.956 source-audio seconds,
executes 66,192 FFT jobs/39 transforms over 208 owners (allocated scorer-slot peak
eleven; actual utilization unavailable), and
uses 562.536933 external CPU/75.849834 wall seconds, 731,871,378 peak DSP bytes
within 2 GiB, with zero drops/resets/search-limit flags. Affinity 0–11 supplies
six physical cores/twelve logical CPUs. This is not a causal CPU gain over the
older one-core R24 run or device real-time proof; simulated backlog zero does
not certify the hardware pipeline. [Inputs](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/qualification-hf40-r1/inputs.json)
and the [terminal guard receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/qualification-hf40-r1/terminal.json)
bind source/core/GUI/test/key hashes and the actual launch (the key is a lab key):

```sh
taskset -c 0-11 build/dev/test_helper_normal_acquisition --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 360 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir /home/user/___quick/p/_cur/dataPump/pumpModem/local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/qualification-hf40-r1/full
```

The [main build/target receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/build/terminal.json)
passes with 146.247762 aggregate CPU seconds and unchanged source guards.
[Targeted continuity/loss and GUI checks](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/focused/results.json)
pass, GUI self-check using 8.98 CPU seconds. After the 63,784-byte optional
producer is reclaimed, minimum publication (1,016 bytes) and essential DATA
provenance (28,696 bytes) remain funded as mandatory state. The initial focused
default-LF assertion failure remains preserved; its test-only one-LF expectation
repair binds source `36eb0dd2…`/binary `2eadcfb5…`, while core/GUI stay unchanged.
The full contract below has not been rerun on this app.

The [preceding DATA trace audit](../local-evidence/helper-two-tone/record-frequency-integration-r1/main-integration-r2/qualification-hf40-r1/R6-current-trace-join.json)
does **not** establish RF-work savings from its 609 FFTs. Four early owners do
zero FFT work: `max(U,cell)` gives no headroom when U ≈1.114 Hz exceeds the
0.097 Hz cell allowance, then 252 outside-constructor pauses suppress work.
All later executed work is identical to the retained comparison. This remains
a guarded availability defect in that binary; the targeted additive result above
has a separate binding. Earlier R3/R4/R5/R6 failures remain retained.

The [RF-only GPSDO source-window control](../local-evidence/helper-two-tone/architecture-review-r1/rf-only-control/runtime-r2/summary.json)
preserves +100 ppm LF/audio and Default/70 ms settings. It uses 25.176393 CPU
seconds, 5,775 FFT jobs/15 transforms: 55 coupled pairs × seven jobs × 15 executing
owners, with two correct pending bits/no END. Its bank frontier 285,835 differs
from assisted 283,313, so no exact CPU ratio follows; [actual argv/settings](../local-evidence/helper-two-tone/architecture-review-r1/rf-only-control/runtime-r2/inputs.json)
retain the private RF-only override. The [link-output incident](../local-evidence/helper-two-tone/architecture-review-r1/rf-only-control/build-only/link-incident-r1.json)
overwrote the old R4 `link.map`, which is invalid as R4 authority. Source/core/
binary pins remain unchanged; the corrected control uses its own map without
the redundant wrapper. No original map reconstruction is claimed.
A separate full RF-only control based on frozen `85a5` builds, but both runtime
budgets fail incomplete with `SIGXCPU` and no final trace/total-work receipt:

| Retained RF-only control | External CPU / wall | Joined work lower bound |
| --- | --- | --- |
| [250-CPU budget](../local-evidence/helper-two-tone/architecture-review-r1/rf-only-control/full-85a5-r1/runtime/audit.json) | 250.260031 / 37.319753 s | 28,490 FFT jobs/74 transforms, 53 executing owners |
| [900-CPU followup](../local-evidence/helper-two-tone/architecture-review-r1/rf-only-control/full-85a5-r1/runtime-r2/audit.json) | 899.450912 / 132.945986 s | 102,795 FFT jobs/267 transforms, 143 executing owners |

The latter observes canonical eight bits/END but the same ID 1 publishes
`11`→`01`→`11010`, violating the prefix contract. Canonical completion cannot
make this a full control pass. No complete CPU ratio, source-finished result or
post-signal backlog/health conclusion follows. Helper-OFF receives the mixture
unnotched, while the assisted frontend isolates it: that fairness difference
remains unquantified. The current History guard above passes its bounded paired
and GUI checks but has no full 900-CPU RF-only rerun; the original strict failure
and HF15k's independent false-END failure remain unchanged.

The [source-derived R24 tail budget](../local-evidence/helper-two-tone/architecture-review-r1/hf40-tail-budget.json)
requires at least about 191.71 capture seconds versus 162.18 observed. Tail/stage/
envelope runtime flags were unlogged, so this forecast is not proof of the exact
runtime predicate or a deadlock. The original strict failure remains failed.

The source-frozen contract stage is **FAILED: 299 invoked, 288 passed, 11 failed**
(two timeouts and nine assertions), in 3,397.34 wall/10,019.79 CPU seconds.
[Outcomes](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/outcomes.json)
and the [terminal receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/terminal-receipt.json)
confirm the run is reaped, with all 1,169 source inputs and 271 binary pins
unchanged. It binds the `65ee248`/`b0fb5dc` baseline, core `341b2b35…` and GUI
`0feb6a00…`; later focused repairs below do not change this result. Native
display/audio coverage is excluded. Every failed test remains distinct:

| Test | Outcome | Observed scope and incomplete coverage |
| --- | --- | --- |
| [differential_receiver_probability](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/differential_receiver_probability-ctest-receipt.log) | Timeout, 1,500.10 s | Partial calibration only; no final probability assertion or completed remaining internal matrix. |
| [scheduled_parallel_preparation](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/scheduled_parallel_preparation-ctest-receipt.log) | Timeout, 1,500.04 s | Arithmetic controls finish; timeout in `shared.serial`. Shared completion/comparison and later internal cases are incomplete. |
| [helper_normal_loss](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_normal_loss-ctest-receipt.log) | Assertion, 33.55 s | Exact 64 bits/pending/scored END pass, but `phase_active=1` versus legacy `phase_applied=0`/`phase_work=0` fails. Subsequent loss leg is unexecuted. |
| [helper_conditioned_native](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_conditioned_native-ctest-receipt.log) | Assertion, 1.04 s | No required primary conditioning transaction; later native fault/loss coverage is unexecuted. |
| [live_helper_clock_bridge](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/live_helper_clock_bridge-ctest-receipt.log) | Assertion, 0.01 s | Owned frequency `SourceDate` marker-support assertion; later bridge cases are unexecuted. |
| [helper_cohort_recovery](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_cohort_recovery-ctest-receipt.log) | Assertion, 0.92 s | Fixture does not reach a narrowed native cohort batch; recovery tail is unexecuted. |
| [helper_punctuation_clock](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_punctuation_clock-ctest-receipt.log) | Assertion, 0.02 s | Actual-marker versus carried-publication assertion; later clock cases are unexecuted. |
| [helper_local_aperture](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_local_aperture-ctest-receipt.log) | Assertion, 0.01 s | Qualified joint phase-support fit absent; following aperture cases are unexecuted. |
| [helper_payload_tail_retune](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_payload_tail_retune-ctest-receipt.log) | Assertion, 0.12 s | Filter exactness/transition controls finish, then DATA-flat-passband admission fails; later retune payload/tail coverage is unexecuted. |
| [helper_live_assistance](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_live_assistance-ctest-receipt.log) | Assertion, 46.43 s | 1,024-byte text stops at 12,340/17,024 bits before physical source end: 3,954.85 versus needed 5,458.8 audio seconds under its 45 s allowance. No large backlog is proved; full text/END and later attachment coverage are absent. |
| [helper_receiver](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/helper_receiver-ctest-receipt.log) | Assertion, 72.69 s | Weak TIMECODE frequency case fails at line 709. Earlier below-nominal zero references are the expected passing control, not this failure; subsequent cases are unexecuted. |

The [failed-trace preservation receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/final-failure-trace-preservation.json)
keeps each source-bound result. An
[attempted pretest hold](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/intentional-hold.json)
was ineffective because the guard ignored a new noncompiled sentinel. This
hold-mechanism setup failure is separate from the eleven contract failures.
The sentinel was removed; the [post-hold comparison](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-coast-r1/post-hold-binding-check.json)
confirmed the original inputs/apps unchanged before the coordinator continued
that run. Private whole-score/gate/terminal prototypes remain unpromoted.

The later [focused repair receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/focused-current-r1/terminal-receipt.json)
binds core `72abf76e…`/GUI `14b44569…`, with unchanged source/app/test guards.
Application and four-target builds pass in 86.41/13.18 wall seconds
(87.36/24.67 CPU seconds). Exact support-substring and headless GUI self-check
pass. `helper_normal_loss` passes in 67.86 s and `live_helper_clock_bridge` in
2.67 s; `helper_punctuation_clock` fails in 0.04 s on its actual signed
clock bound. The selected CTest group uses 67.92 wall/13.22 CPU seconds, without
comparison attribution or an overall pass. Native/cohort callback coverage
remains unreachable/unfixed; none relabels the old 288/299.

The [local-aperture/passband focused receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/local-passband-r1/integrated-focused-r1/terminal-receipt.json)
retains unchanged 1,169-source/six-binary pins. Two targets build in 7.52 wall/
10.40 CPU seconds. Payload-tail retune passes in 2.66 seconds; local aperture
fails its first 30 dB-Hz seed with 11 frequency hops, five phase-eligible and
six frequency-only, zero exclusions/wrong tones. That revision's new ≥6 phase
gate conflicts with separate frequency authority. The 2.687 wall/
2.756 CPU-second group does not execute the remaining matrix or new frequency-gap
negative case.

The [punctuation-date diagnostic](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/punctuation-clock-dates-r1/measured-diagnosis.json)
contains the configured +100 ppm in [80.762640,119.231871] ppm, width 38.469 ppm,
using 14 unique markers and 78 carried publications. All marker errors are below
0.000445 sample versus uncertainty ≥1 sample. The final carried publication is
rejected solely because its marker age is 1,191 samples against the unchanged
1,000-sample limit. The [fixture-only repair](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/authority-separation-r2/helper_punctuation_clock-ctest-receipt.log)
passes in 0.06 seconds: the positive fit uses the actual last distinct publication
and a separate negative checks carried staleness. No production change follows.

The same [R2 local-aperture check](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/authority-separation-r2/helper_local_aperture-ctest-receipt.log)
fails its newly added gap-positive case: at 48 kHz/4,096 samples, a 23.4375 Hz
radius on the fixed 0.1 Hz grid needs 469 cells against a 251-cell budget, so it
declines before fitting. This is an implementation limit, not a physical SNR
floor or successful gap-positive coverage. The [R3 check](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/local-aperture-r3/ctest-LastTest.log)
passes in 1.99 seconds, retaining that unsupported decline without authority,
then recovering causally from a straight 200 ms aperture [93,699,103,299): error
0.000763273 Hz versus U 0.0524458 Hz, without reset, truth seed or storage growth.
Its 40 channel/noise cases have zero exclusions, wrong tones or cold-noise fits;
the separate drift case has eight fits and zero exclusions. Both receipts retain
unchanged source/binary guards and core `72abf76e…`/GUI `14b44569…`; these targeted
passes do not relabel the failed full contract.

The [unchanged weak-case descriptor](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/weak-descriptor-r1/descriptor-summary.json)
records 31 looks from 8–20 seconds, all `presence_unqualified` before precision
cells (0/20,480), with zero commits. The zero SNR field is an unmeasured sentinel,
not a C/N0 measurement. The diagnostic uses 3.055 CPU seconds and establishes
no physical SNR floor or successful frequency acquisition.

The rebuilt GUI is `build/dev/datapump-gui`. The preceding coast/support-API
[app build](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/apps-result.json)
passes in 162.506 wall/317.220 CPU seconds, with
[GUI `0feb6a00…`/core `341b2b35…` and source pins](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/apps-source-binding.json).
Its [headless self-check](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/gui-self-check-result.json)
passes in 7.114 wall/6.991 CPU seconds, without display/audio-device coverage.
The [focused checks](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/focused-results.json)
all pass: protocol, future-scoring support and both default actual-RF Sessions,
4.87 total process CPU seconds. [Detailed RF output](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/actual-rf-defaults-detail.log)
retains exact `001`, pending/scored END and zero reported escapes in both cases.
The independent-clock constructor is 437→5 pairs with the original ±300 ppm LF
domain retained; it executes 7,483 FFT jobs/197 transforms and 2.30484 fixture process
CPU seconds, without a matched CPU-gain claim. Neither normal case has pre-fit
coast events; the separate focused comparisons below exercise the new routes.

Frequency availability no longer requires a current-hop full fit/presence
bootstrap. It is bounded by **0.5 seconds from independent frequency evidence**
with integer sample dates; retunes never renew the witness. Three-second
mathematical prediction/history, explicit close/reset/veto and phase's own fit
requirements remain. Primary independent source review accepted this change;
the additional requested Astra review could not initiate because of reported
model capacity, with no approval denial reported. The const native/compact
earliest-future-frequency-scoring-support API passes its
[focused test](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/support.log).
It is a receiver-local support bound, not a processed/capture frontier, phase or
END certificate. That preceding app's RF mask windows are not whole-score
frequency certificates; R4's selected coverage and functional failures are
recorded below, while the DATA-specific lease remains private. Known-null
settlement alone cannot replace history through Hilbert/FIR transients.
A private [whole-score/frontier experiment](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/build-and-live/runtime-results.json)
**fails its performance acceptance**. The baseline completes source/bank fence
285,835/283,313 in 24.704222 CPU seconds. The new branch reaches its 75-second
CPU cap (75.104459 seconds, exit −9) before its unjoined final first-FFT work
completes; it has no completed current fence/digest or final FFT total. The
[partial-trace audits](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/build-and-live/)
retain 2,241 matching raw records/certificates without unsupported gaps, the
same 60 owners and 1 GiB factory grants. Retaining early ±0.389 Hz support means
445 pairs versus baseline 109; audited pending-drain work is 3.89× slower.
The private whole-score/frontier patches remain unpromoted, without new gain or
qualification. The earlier 9.17% prefix saving retains its original whole-score
limitation; it does not validate this failed branch.

A private [non-hopping refinement probe](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refinement-probe-r1/operational.log)
uses operational positive Q = 0.0009000576 without truth seeding. In one seed,
186/197 qualified spans improve: the span hull changes from ±0.21225 Hz to
[−0.098664,+0.103985] Hz, with zero exclusions in 539,348 overlapping endpoint
checks. Those checks are not independent coverage trials; 42 unsupported spans
remain unsupported. The separate
[Q=0 laboratory control](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refinement-probe-r1/constant-drift-lab.log)
fails, retaining 61 raw-poll and at least 62 refined-span exclusions. The
[results and bindings](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refinement-probe-r1/)
do not qualify Live whole-score support or authorize a Q=0 production rollback.
The first private refined-Live source was rejected on model-veto lifecycle
handling. Its [matched R2 runtime](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r2/matched-72ab-r1/matched-live-analysis.json)
**fails cost acceptance** against core `72ab…`. Both arms hash identical physical
PCM [0,1,143,721), SHA `d3f3749e…`, and join source/bank 285,835/283,313 at revision
1/TX epoch `1800000005`. FFT jobs rise **10,682→35,672 (+233.94%)**;
[external CPU](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r2/matched-72ab-r1/runtime-results.json)
rises 23.731863→65.756964 seconds (wall 23.577→65.602 seconds). Both retain only
two of eight bits and intentionally stop incomplete, without payload/END proof.
Inputs, key hash and source/binary guards pass with no runtime error. This
constant fixed-RF prefix comparison is not general veto safety, Live gain or
promotion; its trace audit remains pending.

The [R3 veto discriminator](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/frequency-veto-r3/checks/repair-results.json)
retains expected old failure/new pass: the independent-frequency veto counter
increments 0→1 at inclusive boundary 104,191 and revokes older frozen authority.
Replacement support [104,192,113,792), captured at 114,240, has error 0.000166375 Hz
versus U 0.100391165 Hz. Its pure-phase control leaves the counter zero and older
frequency knowledge intact. Compilation uses 9.548 CPU seconds; total private
runtime uses 0.060 seconds. The [main counter-fix build/checks](../local-evidence/helper-two-tone/record-frequency-integration-r1/contract-fixes/main-veto-r3/terminal.json)
pass: `./build.sh` uses 73.837 wall/78.328 CPU seconds, `helper_continuous`
1.405 wall/1.406 CPU seconds and headless GUI self-check 8.285 wall/8.164 CPU
seconds. The receipt binds core `492851dc…`/GUI `43e523e8…` with no runtime-source
changes during testing. This target still retains the deliberate zero-process
late-drift control's 48 exclusions and one model reset; only the new counter
controls have zero exclusions. Its constant-Q0 128-second refit is not FHSS or
positive-Q qualification. Native display/radio and general R3 Live safety remain
unvalidated; the original full contract remains failed.

The [private matched R4 prefix](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/matched-analysis.json)
uses the same `d3f3749e…` PCM/fence/TX epoch as R2 above. All 2,241 raw, 4,467
buffered, 138 corrected records and 60 constructors are identical, with the same
14 owners, nine unqualified LF alternatives, 14 transforms and one worker slot.
The valid whole-score hull changes [−0.313923139,+0.307877191]→
[−0.257774507,+0.179742089] Hz: 364→268 pairs and **35,672→26,264 FFT jobs
(−26.37%)**. [External CPU](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/runtime-results.json)
falls 65.756964→49.005478 seconds (−25.47%). Both retain the same incomplete
two-bit prefix/identity, without END or drops/resets. The older 109-pair mask
does not cover the whole score and is not a valid R4 cost baseline.
DSP workspace rises 613,155,001→613,239,457 bytes (+84,456); tracker storage adds
29,184 bytes, with 63,784-byte producer/63,808-byte snapshots, at most 256 recent
numeric descriptors/4.8 seconds and the unchanged 400 ms PCM stage.

R4's unchanged private continuous target passes in 1.505 CPU seconds, while
[64-versus-256-node evidence](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/unit-64-vs256-escape-proof.json)
retains the identical 14 positive-Q smoothed-query exclusions among 34,623 queries
(maximum ratio 1.1231), 6.6–7.3 seconds after drift-rate onset, not immediate
onset latency. Eight short seeded cases have zero exclusions in 1,582 correlated
descriptors per arm; no known-truth precision claim follows for the matched Live prefix.
These are conditional intervals, not calibrated confidence. R4's
[main build](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/main-integration/build/results.json)
passes in 113.904 wall/113.649 CPU seconds. Its
[focused receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/main-integration/focused/terminal.json)
retains unchanged source/binary pins: **4/5 pass**, 69.378 wall/14.687 CPU seconds;
`helper_normal_loss` fails at 60.02 seconds. GUI self-check passes in 11.092 wall/
12.010 CPU seconds. [Bindings](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/main-integration/focused/before.json)
identify core `15673871…`/GUI `2630c59f…`; manual display/radio remains untested.

The [normal-loss predicate diagnosis](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/main-integration/focused/normal-loss-diagnosis/analysis.json)
joins all 112 dated phase certificates correctly; 111 fail only the fixture's
`locked` predicate, which instead describes whole-history RF authority. The
reviewed sole-predicate repair `9695227a…` followed this failed run; the current
funded-provenance checks above have their own binding. That repair alone does not
repair the separate [zero-work failure](../local-evidence/helper-two-tone/record-frequency-integration-r1/whole-score-r1/refined-live-r4/main-integration/focused/normal-loss-diagnosis/no-progress-analysis.json):
the 63,784-byte score producer yields at sequence 108 for mandatory birth,
leaving 13 conditional owners with zero FFTs/no bits/END. Joined input reaches
28.815 seconds versus nominal DATA 6–16.24 seconds plus six-second END support;
elapsed input does not establish scored coverage. This is a provenance-funding
failure, not a physical SNR limit. R4's matched-prefix stage CPU records generation
1.061884 s, ordinary feed 47.115299 s and outside-feed 0.638406 s; isolated
frontend/optional/kernel CPU is unavailable.

The separate [private actual-DATA R3 experiment](../local-evidence/helper-two-tone/record-frequency-integration-r1/actual-data-lease-r3/runtime-results.json)
fails with zero owners/FFT/exact payload/END despite 77 qualified outputs and
504 covered raw projections. Diagnosis finds a funding/bootstrap-capacity
initialization cycle. The [R4 run](../local-evidence/helper-two-tone/record-frequency-integration-r1/actual-data-lease-r4/joined-runtime-audit.json)
retains a separate one-ULP containment failure. The
[R5 numeric component](../local-evidence/helper-two-tone/record-frequency-integration-r1/actual-data-lease-r5/checks/comparison.json)
passes 197 cases: seven old declines recover, all 83 retained outputs stay
bounded, strict guards and exact-zero behavior remain. Component correctness
does not erase the [R5 Live failure](../local-evidence/helper-two-tone/record-frequency-integration-r1/actual-data-lease-r5/execution/joined-runtime-audit.json):
no exact payload/END, 5,859 FFT jobs/27 transforms/13 owners and 2.338238 external
CPU seconds, versus the retained passed R3 baseline's 2,226/50/14 and 1.803132 s.
This is not qualified savings. Actual-TX owner 9 freezes constructor bounds
[−0.1225269995,−0.0774867657] Hz; a later supported whole hull is about
0.000071882 Hz wider at either edge, so this owner performs zero FFTs. This is
immutable-constructor lockout, not a physical precision limit. All three DATA LF
cells retain the full ±300 ppm domain without clock qualification; the R3
M-coupled baseline has five or seven representatives (actual TX five), not three.
Earlier funding, one-ULP and functional failures remain distinct. The current
additive result above has a separate binding and measures restored work; it
does not relabel these original failures.

The strong actual-RF baseline measures about 55.20 dB-Hz. It is not the supplied
HF40 case's configured total/pilot 30.02/29.56 dB-Hz, sampled pilot 23.54 dB-Hz
or first measured 23.58669 dB-Hz. Existing `gpsdo_reference` controls improve both
RF and audio oscillators while retaining Default/70 ms settings; the new RF-only
control above retains audio but still lacks an identical scored frontier. HF40
hardware throughput, HF15k wrong-calendar END, EME gated-template/memory/CPU and the
1,024-byte cutoff remain separate failures; no physical floor or full
HF/128-second EME/native qualification follows.

The independently reviewed [matched actual-RF final-loss comparison](../local-evidence/helper-two-tone/record-frequency-integration-r1/frequency-coast/matched-final-loss/measured-comparison.json)
holds physical PCM [0,900,000) equal (SHA `5398cc4c…`) and final media dates
equal. Both retain exact `001`, all prefixes on ID 2 and fully scored END.
The new path adds 30 prior-only frequency corrections, including five after
final Helper loss in hop 28. Their ages 0.436/0.452/0.468/0.484/0.5 seconds use
the same witness [13,184,332,672), with U 0.02015→0.02169 Hz, no renewal, no later
phase and zero reported escapes. FFT jobs rise **2,058→2,226 (+8.16%)** and
external CPU 3.074368→3.107025 seconds; peak workspace grows 75,680 bytes, with
the raw stage still 400 ms. This demonstrates bounded support, not CPU gain.
The hash covers END dependencies through physical sample 618,937, but not the
last-work physical equivalent 915,984 or tail [900,000,931,213); it establishes
no full-tail equality claim.

The separate [strong fixed-RF gap comparison](../local-evidence/helper-two-tone/record-frequency-integration-r1/matched-live/physical-ambiguity/analysis.json)
uses old `b3b5…`/record-fallback `e25…`, with the same raw prefix [0,291,853)
(SHA `e2c6f8b9…`), source/bank 72,868/72,680 and TX epoch `1800000006`.
An engineered 280 ms accepted-projection gap yields **56 independent-aperture
publications versus zero**, all with invalid previous plans; 17 join reports
whose phase fit is false. Maximum nominal-flank frequency error is 0.00038353 Hz
versus U 0.10148–0.21848 Hz, with no interval exclusions. Pilot C/N0 median
71.97 dB-Hz makes this a strong branch test, not 30/40 dB-Hz sensitivity evidence.
Both execute 1,127 FFT jobs/23 transforms in about 0.687 CPU seconds, while phase
applications differ 52→54; no RF-only CPU causality is claimed. All 42 LF count
reductions in each case have independent qualified clock evidence; zero
unresolved-LF drops occur. The original strict all-counts-unchanged analyzer
failure remains retained. One later birth genuinely changes 7→11 pairs at the
same grant. Only the same wrong-epoch pending one-bit suffix (epoch 4 versus TX
6) appears: this is not canonical `001`, full payload or physical-END acceptance.

The preceding record-fallback
[application build](../local-evidence/helper-two-tone/record-frequency-integration-r1/final-build-results.json)
has a [source/binary receipt](../local-evidence/helper-two-tone/record-frequency-integration-r1/final-build-binding.json),
but does not bind every subsequent checkpoint/overlay edit. Repaired
[stage/forecast checks](../local-evidence/helper-two-tone/record-frequency-integration-r1/repaired-r2-results.json)
pass in 1.67/4.73 wall seconds under their
[specific binding](../local-evidence/helper-two-tone/record-frequency-integration-r1/repair-r2-binding.json).
The [latest completed GUI group](../local-evidence/helper-two-tone/record-frequency-integration-r1/gui-group-result.json)
is **38/39 in 237.06 seconds**. Its original LPI restore-target failure is
retained: [full-context diagnosis](../local-evidence/helper-two-tone/record-frequency-integration-r1/gui-lpi-diagnosis/full-context-run.log)
finds fixture geometry changed from 60 to 63.0957-second symbols, so the
442×/464× advisory comparison does not hold geometry fixed. The
[geometry-repaired full controller](../local-evidence/helper-two-tone/record-frequency-integration-r1/lpi-fixture-verification/result.json)
passes in 138.40 seconds, together with protocol, on the preceding `e25…` core.
This isolated repair is not a new 39/39 run on the coast build. These are selected source-
bound results, not current exact-app, full-stage or sanitizer qualification.
The [repaired stage output](../local-evidence/helper-two-tone/record-frequency-integration-r1/stage-repaired-r2.log)
also retains **35 snapshot/four dated escapes** in its laboratory 60 dB-Hz
actual-RF case. Known retune dates/USB/zero audio-clock mapping and isolated
score diagnostics do not establish general Live fallback coverage.

The earlier fixed-lag
[final build](../local-evidence/helper-two-tone/lag-frequency-integration-r1/final-app-build.log)
is bound to [GUI `e008fb79…`/core `b3b5d52c…`](../local-evidence/helper-two-tone/lag-frequency-integration-r1/final-build-binding.json),
with [headless self-check](../local-evidence/helper-two-tone/lag-frequency-integration-r1/gui-self-check-result.json)
passing in 9.638 wall/9.488 CPU seconds; native display/audio testing is unavailable.
The [focused group](../local-evidence/helper-two-tone/lag-frequency-integration-r1/focused-tests.log)
is **7/8 in 39.45 seconds**. Continuous refit/freeze, actual-RF Session/independent,
model/protocol, frequency-window and reference checks pass. `helper_pilot_stage`
gets seven phase updates against its ≥8 assertion; an
[old-1745 contextual run](../local-evidence/helper-two-tone/lag-frequency-pilot-stage-baseline-r1/old1745-firstcase.log)
reproduces the same failure, separately from fixed lag. Original failures remain.
The preceding admission/raw-origin
[app build](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-r2-app-build.log)
passes, bound to [GUI `6d8c0973…`/core `1745de63…`](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-r2-binding.sha256).
[Focused results](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-r2-focused-results.json)
pass protocol, actual-RF Session/independent-RF and headless GUI self-check;
the original reference-fixture failure and its geometry-only migration are
recorded below. Its [full GUI group](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-gui-group.log)
passes **39/39 in 217.89 seconds**, with [detailed results](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-gui-group-detail.log).
This is pre-lag coverage, not the current full GUI group.
The preceding filter/translator index-walk
[app build](../local-evidence/helper-two-tone/continuous-process-r22/filter-translator-index-app-build.log)
and [GUI self-check](../local-evidence/helper-two-tone/continuous-process-r22/index-gui-self-check.log)
pass on [GUI `eb5633c7…`/core `9b4174fe…`](../local-evidence/helper-two-tone/continuous-process-r22/filter-translator-index-app-binding.sha256).
[Eleven focused tests](../local-evidence/helper-two-tone/continuous-process-r22/index-focused-tests.log)
pass 11/11 in 101.20 seconds, with [detailed results](../local-evidence/helper-two-tone/continuous-process-r22/index-focused-tests-detail.log).
The rebuilt index-core [preservation checks](../local-evidence/helper-two-tone/continuous-process-r22/index-preservation-tests.log)
pass **13/13 in 508.49 seconds**, with [detailed output](../local-evidence/helper-two-tone/continuous-process-r22/index-preservation-tests-detail.log);
this selected contract scope does not complete the full contract stage.
The prior dated-frequency [self-check](../local-evidence/helper-two-tone/continuous-process-r22/dated-frequency-gui-selfcheck.log)
and [binding](../local-evidence/helper-two-tone/continuous-process-r22/dated-frequency-binding.sha256)
retain GUI `4410c404…`/core `9e844a2d…`; their five targeted results below do not
bind the new index loops.
The preceding implicit-RF [GUI group](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-gui-group.log)
passes **39/39** in 205.80 seconds on core `3a7b90ec…`, before the dated-frequency
patch. The new [targeted checks](../local-evidence/helper-two-tone/continuous-process-r22/dated-frequency-tests.log)
pass 5/5 in 45.61 seconds: protocol/RF/independent-RF use core `9e844a2d…`;
frequency-window/loss binaries retain the prior core with unchanged relevant
paths. These retain their earlier binding, separately from the current all-39
pass. The repair keeps current
references closed-gated but permits previously captured direct-aperture support
after later shutdown; closed saved queries require dates strictly before the
saved pilot end. It preserves dated provenance without manufacturing phase.
Earlier [GUI/source bindings](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-app-binding.sha256)
retain GUI `1a8f967b…`/core `3a7b90ec…`; mixed post-test snapshots are not test seals.
Normal [frequency-window/GUI Planner checks](../local-evidence/helper-two-tone/continuous-process-r22/planner-window.log)
pass in 36.39/71.65 seconds. The [focused Live group](../local-evidence/helper-two-tone/continuous-process-r22/live-focused.log)
is **5/7** (101.30 seconds): `helper_live` passes in 39.05 seconds including
wide +100 ppm callsign reception; independent-RF, GPS, Default and final-loss
pass. Its RF-only failure has zero raw references after positive-Q pooling was
disabled; the targeted fallback checks below now supersede that failure.
`helper_live_assistance` reaches its
45-second deadline with correct observed diagnostic prefixes at 7,928/17,024
bits, stable pending id 2/revision 1, zero missing and no completion/END. Its
[captured-input assessment](../local-evidence/helper-two-tone/continuous-process-r22/full-payload-diagnosis/assessment.json)
and [prefix/frequency checks](../local-evidence/helper-two-tone/continuous-process-r22/full-payload-diagnosis/prefix-frequency-check.json)
show ongoing waveform generation/progress; captured minus feed is only 325
samples/0.1625 seconds at 2 kHz. This is unfinished coverage, not a demonstrated
input backlog or completed-content failure. A
[superseded 77-second diagnostic](../local-evidence/helper-v15/legacy-live-default-r1/long-text-cutoff-diagnostic-r1/execution/case.wrapper.log)
completed 17,024 bits then hit legacy admitted/promoted assertions, but its
1050 versus current 1450 Hz pilot and sources do not validate current SF32
completion/performance. No additional long run is justified solely by that cause.

The later normal [retune-fallback checks](../local-evidence/helper-two-tone/continuous-process-r22/retune-fallback-tests.log)
pass **3/3** in 7.75 seconds: RF-only Session 3.13, independent-RF 3.65 and
continuous 0.97 seconds, including the final empirical ≥99% coverage sentinels.
[Detailed output](../local-evidence/helper-two-tone/continuous-process-r22/retune-fallback-tests-detail.log)
restores 118 qualified raw references in each Session. RF-only retains exact
`001`, pending 3/full END through 73 retunes, 71 model updates/four RFs and
1,582 corrections, with zero reported escapes; max raw error 0.000588588 Hz,
model U 0.0106655–0.0458286 Hz, correction error 0.000665781 Hz and 39,725,681-byte
workspace. Independent-RF retains exact `001`, pending 4/full END, model U
0.007523–0.0455479 Hz, correction error 0.0225418 Hz, zero escapes and
39,955,667-byte workspace. Both raw stages peak at 12,800 samples. The 3,710/
7,483 FFT jobs are not gain claims; 2.76456/3.27481 CPU seconds were measured
concurrently with a build and do not qualify comparative throughput. Exact final
source/binary bindings are pending: the post-test snapshot was collected after
a later implicit-selector header edit and is mixed-state, so it is not used as a
test binding. The new implicit native RF-only selector
[focused test](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-focused.log)
now passes existing/new 5→1 narrowing, exact 12 bits, pending identity, physical
END and loss. Before loss it executes 64 existing/48 new-owner FFT jobs versus
240 in the unassisted control; whole-window counts are 64/48 versus 320. This
is actual work reduction, without a process-CPU saving claim.
A source-bound [matched prefix analysis](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-live/matched-pair-analysis.json)
uses pinned implicit core `3a7b90ec…` and holds captured/source frontier 513,157,
processed frontier 512,936/256.468 audio
seconds and 221 staged samples equal. It preserves one owner, the one original
LF alternative, zero drops/resets, all 785 detector bits and all 260 GUI prefixes
verified offline with the actual encoder. Enabled/disabled execution is
**3,930/19,650 FFT jobs (80% fewer)** with 786 transforms each, but process CPU is
4.435823/4.470645 seconds (0.0172958/0.0174316 per processed audio second).
That approximately 0.8% single-pair difference is not meaningful throughput gain.
All three original wrapper exit-1 failures remain: they mistakenly compared
pre-XOR detector bits with post-XOR GUI truth. Offline coordinate correction
validates the unchanged captured detector/GUI prefixes; it supplies no new timing
run or full physical END result.

The [nested hot-loop analysis](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-live/hotloop-analysis.json)
uses 4.575706 process CPU seconds. Inclusive native frontend push uses 2.188459
seconds (about 48%), optional auxiliary advance 0.956479 (21%), tracker push
0.134061 (3%), and channel generation excluding observation 0.778840 (17%).
Clock failures are zero. These nested scopes overlap and must not be summed.
This refutes replay dominance in this profile. Two small index-walk changes
are now integrated: the Helper filter replaces paired ring modulo operations
with wrapping indices; the Hilbert translator replaces paired modulo-1025
operations with stride-two wrapping. Kernels, coefficients, delays and estimator
arithmetic are unchanged. [Filter controls](../local-evidence/helper-two-tone/continuous-process-r22/filter-index-cpu/)
cover 32 matched cases at 8/48.96 kHz, widths 225/425, real/random input, four
chunk patterns and four retunes, preserving float bits, coefficients, dates,
counts and storage. At width 425, four-second component CPU is
0.019888→0.004001 seconds at 8 kHz and 0.630545→0.135801 at 48.96 kHz (about 4–5×).
Translator controls preserve all PCM/state bits across chunks 1/137/1024/rate,
conjugation and phase/frequency updates: aggregated 64-second CPU is
[0.167922→0.107924 at 8 kHz](../local-evidence/helper-two-tone/continuous-process-r22/translator-index-cpu/8000-8s-result.json)
and [1.083390→0.631101 at 48.96 kHz](../local-evidence/helper-two-tone/continuous-process-r22/translator-index-cpu/48960-8s-result.json).
Older fixtures pass. The [matched Live AFTER](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-live/filter-after/after-analysis.json)
now passes against the retained enabled BEFORE, with [evidence bindings](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-live/filter-after/evidence-sha256.json)
to current core `9b4174fe…`. Capture/processed/staging coordinates remain
513,157/512,936/221, with 785 identical detector bits, all 260 correct GUI commits
on id 2/revision 1, and all 4,012 reference measurements/support dates identical
(3,998 qualified). Work is unchanged at 3,930 FFT jobs/786 transforms, one owner,
5→1 RF and the one original LF alternative; no drops/resets/errors occur.
Fixed-prefix process CPU falls **4.435823→2.610347 seconds (41.15%)**,
0.0172958→0.0101781 CPU per processed audio second; prefix wall falls
4.340374411→2.528036488 seconds. This is measured process improvement in this
profile, not a general receiver-performance claim. Baseline PCM was not hashed:
component PCM/state equality and Live reference/prefix equality are demonstrated,
without claiming whole-Live PCM-byte identity. There are no actual-RF/model events
in this fixture; its dated model is inactive. Current 1,024-byte completion is
untested, and the older-core 45-second failure below remains separate.

The [full-payload cutoff remains failed](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-live/full-payload-failed-summary.json)
at 45 seconds: 7,770/17,024 bits, stable id/revision and matching recorded
128-bit prefixes, no END, one owner narrowed 5→1, 38,320 FFT jobs/7,664 transforms,
45.7303 CPU/45.0036 wall seconds and 0.1295-second captured/feed gap. Its
[compressed trace](../local-evidence/helper-two-tone/continuous-process-r22/implicit-rf-live/full-payload-failed-45s.jsonl.gz)
retains the incomplete result; no completion or CPU gain follows.

The migrated [realistic/calendar Live diagnostics](../local-evidence/helper-two-tone/continuous-process-r22/migrated-live-focused/results.json)
both fail with no bits/END on core `9b4174fe…`. The
[trace assessment](../local-evidence/helper-two-tone/continuous-process-r22/migrated-live-focused/assessment.json)
finds 4,535 qualified pilot observations but only 42 FFT jobs in the realistic
case. The calendar assertion incorrectly rejects a qualified-clock flag even
though all three original LF alternatives remain; it also has no bits/END, so
relaxing that assertion would not establish reception. Width-only prior
retention keeps the nominal GPSDO cells despite a deliberately injected
+2.17 Hz offset outside their prior. The location-compatibility repair now passes
focused ±2.17 Hz/contact controls. Its first
[reference-fixture failure](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-reference.log)
uses a retired 2 kHz single-tone placeholder; the
[context discriminator](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-context.log)
finds its auxiliary falls in DATA. Migrating only that frontend placeholder to
`helper::plan` two-tone placement, with unchanged U/offset/configuration/bounds,
[passes geometry controls](../local-evidence/helper-two-tone/continuous-process-r22/prior-location-geometry.log).
That reference binary was recompiled after the app binding; the original failure
remains. Retained-backend construction separately passes five ordinary complex
numeric owners before symbol scoring. The
[migrated rerun](../local-evidence/helper-two-tone/continuous-process-r22/migrated-live-focused/prior-location-repair/results.json)
passes realistic reception: exact one bit/END, 98 joined FFT jobs and one owner.
Calendar remains **failed** at its original 60-second ON allowance, with a stable
exact 11/12-bit pending prefix and no END. The
[same-owner support reduction](../local-evidence/helper-two-tone/continuous-process-r22/migrated-live-focused/prior-location-repair/calendar-owner9-support.json)
shows owner 9 fully joined through input 1,214,886, beyond the selected
hypothesis's extrapolated twelfth-bit/absence endpoint 1,172,770; its sixth batch
is incomplete. These are hypothesis dates, not TX truth or proof of scored
absence. TX boundaries and per-symbol scored/rejected/absent decisions were
filtered out, so the cause is unresolved; the 0.516-second capture gap is not
an established explanation.
A separate [fresh-epoch calendar discriminator](../local-evidence/helper-two-tone/continuous-process-r22/migrated-live-focused/prior-location-repair/calendar-score-discriminator/assessment.json)
still fails its original allowance, now with canonical exact 12 bits and scored
END: the twelfth score is 7,349.71 versus 56.07 threshold, and the following
absent score is 3.573 below retain floor 5. Its planned Helper endpoint remains
37.18 seconds beyond captured media. This fresh ON-only PCM is not a matched
replay of the original 11/12 failure. It has 10,198/10,354 qualified pilot
publications, 100% qualification after five seconds, zero phase/model resets and
up to 128.985 seconds coherent support. At actual 30 MHz, positive Q is
0.00090006312656 Hz²/s³ and current endpoint U is 0.01437–0.01957 Hz (median
0.01695). This is model price, not bootstrap failure or a physical SNR floor.
It executes 8,898 FFT jobs/362 transforms over 227 owners, retains all three LF
alternatives in 959 active masks and narrows 100; process CPU is 60.6619 seconds.
No matched gain or strict pass follows.

Current-core [framed-transfer controls](../local-evidence/helper-two-tone/continuous-process-r22/current-tail/results.json)
pass both OFF and ON, with [binary/source binding](../local-evidence/helper-two-tone/continuous-process-r22/current-tail/binding.json).
[Text](../local-evidence/helper-two-tone/continuous-process-r22/current-tail/framed-text.log)
is 18 source bytes/1,216 wire bits;
[attachment](../local-evidence/helper-two-tone/continuous-process-r22/current-tail/attachment.log)
is 17 bytes, `tail.bin`, and 2,432 wire bits. Both validate authentication, exact
source/all wire, kind/filename, stable pending identity and scored physical END;
ON keeps row 2/revision 1, OFF row 1/revision 1.
The ON runs use timestamp zero, Auto/rolling, 1,500 W Helper and 64 MiB, executing
4,940/9,652 joined FFT jobs with measured narrowing. Pair CPU totals are
33.767573/120.037189 seconds. OFF/ON epochs 1800000000/1800000006 and dated wire
differ; these are source-matched passes, not causal matched-CPU comparisons or
qualification of the unfinished 1,024-byte case. Actual commands remain in
the runtime results and individual logs.

The [current strict HF40 run](../local-evidence/helper-two-tone/hf40-r23/run.log)
fails exit 2 at its 360-second deadline, with [launch settings](../local-evidence/helper-two-tone/hf40-r23/launch.json)
and [external usage](../local-evidence/helper-two-tone/hf40-r23/rusage.json) retained.
It captures 145.115 audio seconds and uses 361.48 fixture CPU seconds,
172,289 FFT jobs/202 transforms/158 owners, with zero health faults. The canonical
trace has exact eight bits and scored END, but public delivery/replay remains
unfinished with secondary pending rows. All historically completed rows retain
valid END. Positive-Q uncertainty is wider at actual dispatch than in R15: first
score selects 125 versus 49 pairs. The earlier raw-median precision comparison
did not establish the relevant receiver-work uncertainty and is superseded by
this dispatch audit and the matched comparison below. This is not a new full
reception pass or CPU-gain result.
The fixed-lag [R24 HF40 run](../local-evidence/helper-two-tone/hf40-lag-r24/run.log)
also fails its original 360-second deadline. Its
[launch](../local-evidence/helper-two-tone/hf40-lag-r24/launch.json) and
[usage](../local-evidence/helper-two-tone/hf40-lag-r24/rusage.json) are retained;
the completed [predicate audit](../local-evidence/helper-two-tone/hf40-lag-r24/audit.json)
finds both public exact and the simulation-finish/replay conjunction false.
The fixture accumulates public exact with OR; earlier incorrect completed rows
cannot veto it. Canonical exact eight-bit/scored-END trace commits occur before
staging acceptance and are not public-completion proof. The unmet tail/finish
subcondition is unobserved: individual tail-remaining/envelope/finish flags and
specific canonical staging acceptance were not logged. `complete_tx` needs the
tail/envelope transaction before finish/replay. The run captures 162.1817 audio
seconds and executes 179,546 FFT jobs/239 transforms over 175 owners, with
360.900153 external process CPU seconds; its terminal bank frontier is separate
from capture and does not measure backlog. No strict pass or isolated full-run
CPU-gain claim follows.

The private [actual-Live Q comparison](../local-evidence/helper-two-tone/continuous-process-r22/actual-live-q/matched-analysis.json)
holds raw PCM digest/dates, source/bank frontier, key/profile/epoch domains,
the first 14 owners, grants and all nine LF alternatives equal. Phase correction
is zero in both. Declared Q versus diagnostic Q=0 uses **12,250→4,410 FFT jobs**,
14 transforms each, and **26.425964→12.916053 process CPU seconds**. First-score
selected pairs are 125/45; Q-dependent birth catalogues are 469/451. This
demonstrates the work/CPU cost of current uncertainty on this bounded fixture.
Q=0 invokes the existing zero-Q algorithm and lacks supported frequency-
acceleration uncertainty, so it remains a diagnostic ceiling, not a production
rollback recommendation or drift qualification. Both retain eight startup
escapes from the configured nominal center; that center excludes the sampled
Brownian phase trajectory and is not instantaneous stochastic frequency truth
or calibrated confidence coverage. Both requested-prefix child exits remain
2/incomplete, with no full payload/END claim.

The production fixed-lag [same-cohort comparison](../local-evidence/helper-two-tone/continuous-process-r22/actual-live-q/post-integration/matched-analysis.json)
against that retained positive-Q baseline holds exact raw PCM/dates, source/
bank frontier, first 14 owners, grants and all nine LF alternatives equal.
Phase correction stays off. Q, TX lead and the 400 ms raw stage are unchanged.
Residual U is 0.0795036→0.06640714 Hz, selected pairs 125→109 and executed FFT
jobs **12,250→10,682 (12.8% fewer)**, with 14 transforms each. Process CPU is
**26.425964→24.002513 seconds (9.17% lower)** for this prefix; cache/CPU-frequency
state is not controlled by this later retained-baseline comparison. All 21
buffered nominal-center exclusions before conservative DATA onset remain, as
do the eight raw startup exclusions. These are not instantaneous Brownian-phase
truth/coverage claims. Both pending-prefix exits retain no full END; runtime
invalid joins/unfinished reads leave payload-media/backlog unavailable.

The normal [continuous/refit/freeze output](../local-evidence/helper-two-tone/lag-frequency-integration-r1/focused-tests-detail.log)
has 34,606 narrowed/smoothed of 34,623 valid queries and 14 retained correlated
fixed-lag escapes, while preserving the current/backdated controls. The 132-second
refit passes, as does frozen historical support: a 3.59-second query is unchanged
at capture 5.08 seconds after phase clear, with evidence ending at 3.996 seconds;
the gap gets no smoothed support. Storage is 776,400 bytes, +9,744, without PCM
history. This bounded frequency-only refinement cannot manufacture earlier phase
or advance the processed frontier. The first app build before the optional
append-PSD guard is superseded by the final binding above.

The later [record-fallback matched prefix](../local-evidence/helper-two-tone/record-frequency-integration-r1/matched-live/matched-analysis.json)
holds the same raw PCM/dates/cohort/grants, all nine LF alternatives and phase
off against the retained fixed-lag baseline. Both have 10,682 FFT jobs/14
transforms, 109 selected pairs and U 0.06640714 Hz. All 2,241 raw-reference,
4,467 buffered-frequency and 60 constructor numeric records match; there are
**zero independent-record events**, so no new fallback branch is exercised.
Process CPU is 24.002513→24.810852 seconds (+3.37%), without controlled cache/
frequency state or a gain claim. The requested prefix remains incomplete,
without full END. [Numeric equivalence](../local-evidence/helper-two-tone/record-frequency-integration-r1/matched-live/numeric-equivalence.json)
and [runtime bindings](../local-evidence/helper-two-tone/record-frequency-integration-r1/matched-live/runtime-artifacts.sha256)
retain the comparison's scope.

A private [four-second quadratic GLS comparison](../local-evidence/helper-two-tone/local4-r1/compare-r2.log)
improves steady U but fails late 0→0.03 Hz/s drift: 17/420 distinct intervals
escape at 30 dB-Hz and 40/423 at 40 dB-Hz, with an episode up to 2.69 seconds.
The 60 dB-Hz cable-like case also has 4/141 escapes. Source/binary bindings are
retained in [the experiment](../local-evidence/helper-two-tone/local4-r1/);
this alternative is not promoted.

The current [conditional subset](../local-evidence/helper-two-tone/continuous-process-r22/migrated-conditionals/results.json)
has three passes and one failure on core `1745de63…`, with
[bindings](../local-evidence/helper-two-tone/continuous-process-r22/migrated-conditionals/binding.json).
Measured-reference conditioning passes exact `001`/pending/END with 3,045 FFT
jobs and 0.772542 process CPU seconds; fallback passes captured/processed progress
with 210 FFT jobs and 0.272499 CPU seconds, without bits/END; finite replay passes
in 2.866526 CPU seconds. The Helper-OFF positive control remains failed at its
original 45-second deadline. Its
[trace audit](../local-evidence/helper-two-tone/continuous-process-r22/migrated-conditionals/conditioned-control-audit.json)
finds stable id 1/epoch1800000000 progress `0`→`00`→`001`, zero missing bits and
no END. Five owners each retain 925 ordinary pairs: complete-feed totals are
226,625 FFT jobs/35 transforms; terminal totals 231,997/36 include 5,372 executed
jobs in an interrupted eighth scan. Joined bank frontier 22,849 is 5.71225 audio
seconds. Physical payload end 16,385 plus ten whole 0.64-second absent-symbol
durations requires earliest frontier 41,985/10.49625 seconds. Required END support
has not been processed, so no canonical-bit error is needed to explain this
deadline. Exact final captured count/backlog and eventual correct completion
remain unproved; the failed original control is not relabeled passed.

The private [raw-origin comparison](../local-evidence/helper-two-tone/continuous-process-r22/phase-independent-live/retune-variability/matched-analysis.json)
retains the BEFORE failure and passes AFTER: 29 identical raw/model observations
across changing physical RF, independent phases, +100 ppm and eight jitter-driven
model-epoch changes preserve 20 old-epoch corrections inside actual measured
apertures, versus zero before, with zero escapes. All LF alternatives remain;
266 FFT jobs/two transforms/12 owners are unchanged. [Runtime results](../local-evidence/helper-two-tone/continuous-process-r22/phase-independent-live/retune-variability/runtime-results.json)
use 1.217095→1.288731 CPU seconds, so no CPU gain follows. Workspace grows 144
bytes and raw staging remains 400 ms. Both variants share the private r1 prior-
location patch; only the adapter differs (`0d27…`→`8cb4…`), as checked by
[source equality](../local-evidence/helper-two-tone/continuous-process-r22/phase-independent-live/retune-variability/semantic-source-equality.json).
It separates model revision from provider generation without bridging unknown
phases. Matched PCM bytes were not measured, and this prefix has no full payload/
END qualification. The normal current-core RF controls separately pass exact
`001`, pending identity and END.

A private [phase-publication-off failure](../local-evidence/helper-two-tone/continuous-process-r22/phase-independent-live/matched-analysis.json)
retains 29 identical model updates/four RFs without model escapes, yet zero of
267 existing-mask snapshots narrow and only 41 corrections occur. The
[dated repair comparison](../local-evidence/helper-two-tone/continuous-process-r22/phase-independent-live/repair/matched-repair-analysis.json)
passes 139/297 narrowings, 746 corrections and new constructor 21→17 with all LF
alternatives retained. At the same processed/source frontiers 93,649/96,961,
FFT jobs fall 294→238 (19.05%); 12 owners/two transforms are unchanged.
All 29 phase flags are false and phase updates zero: this deliberately suppresses
phase publication on the same physical independent-phase retunes; it is not a
physical phase-noise/SNR or full-message/END qualification. Whole-child CPU is
1.645882→1.686055 seconds (+2.44%), and affinity differs, so no qualified CPU gain
is claimed. [Repair runtime receipts](../local-evidence/helper-two-tone/continuous-process-r22/phase-independent-live/repair/runtime-artifacts.sha256)
keep both failures and repaired checks source-bound. A separate
[component replay](../local-evidence/helper-two-tone/continuous-process-r22/replay-cpu/run.log)
uses 0.082 CPU seconds/256 audio seconds; its extrapolated share is below 2% of
the full-run CPU, not dominant. No WLS optimization or total-CPU attribution gain
is established.

Earlier R20 [GUI/source bindings](../local-evidence/helper-two-tone/r20-causal-aux-build.sha256)
retain GUI `9cd2dd66…`, archive `f576cb8b…` and Live `06dd6a6e…`.
That reviewed Live change causally centers optional auxiliary ±20 Hz discovery
and the pilot notch from an independently present, qualified dated pilot; it
preserves funding/rollback and grants no frequency/phase authority. That change alone did not fix metadata: the helper-only Live regression (+3 kHz/+100 ppm,
shared-radio 30 MHz, timestamp zero, Auto, 32 MiB, simulation SNR 100, no DATA)
fails callsign decoding after ten seconds on both old R19 and new R20.
[Matched commands/results](../local-evidence/helper-two-tone/auxiliary-wide-live-r20/commands.json),
[old failure](../local-evidence/helper-two-tone/auxiliary-wide-live-r20/old.log)
and [new failure](../local-evidence/helper-two-tone/auxiliary-wide-live-r20/new.log)
remain. The [new trace](../local-evidence/helper-two-tone/auxiliary-wide-live-r20/new-wide-trace.jsonl)
confirms pilot/auxiliary centers 5712.771/5312.771 Hz from pilot date 26,111,
funded at raw sample 26,557. [Pinned-R20 Live diagnosis](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/live-r20-result.json)
and [quantization trace](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/live-quant-r20-run.log)
show no backlog or additional restart: 29 reversals, 28 qualified grids and 28
complete two-tone/metadata fits all fail final APSK/codec decoding; the middle
`N0CALL` symbol is about −0.628 ring cells off. The matched
[zero-ppm control](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/live-zero-clock-r20-result.json)
passes at the same +3 kHz/power/timestamp/budget/thresholds (0.4778 wall/0.3217
CPU seconds), with +0.00816 ring-cell error.

The private R21 repair derives source-local waveform clock `(f_p-f_a)/400`,
anchored at the observed 240 ms reversal, and rescales only optional decoder
windows, 8 ms interpolation and pilot ramps. It supplies no receiver assistance
authority. The exact maintained wide-Live fixture passes against pinned R20 plus
the new decoder object: [result](../local-evidence/helper-two-tone/auxiliary-clock-r21/result.json)
(exit 0, 0.3360 wall/0.328737 CPU seconds, binary `c3ed57df…`),
[inputs](../local-evidence/helper-two-tone/auxiliary-clock-r21/inputs.sha256),
[compile command](../local-evidence/helper-two-tone/auxiliary-clock-r21/compile-command.json)
and [run command](../local-evidence/helper-two-tone/auxiliary-clock-r21/run-command.json).
The final private clock-scale/dedup candidate is independently reviewed:
[source/test pins](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/metadata-clock-final-r21-inputs.sha256)
are decoder `89187a28…`/test `b321df45…`. Its
[two-tone](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/metadata-clock-final-r21-two_tone.log)
and [strong](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/metadata-clock-final-r21-strong.log)
checks each pass all ten geometries (the original eight plus ±100 ppm with
independent hop phases), with seven **unique source dwells** per case. Frozen
snapshot/generation and ±550 ppm actual-Resampler punctuation-clock checks also
pass. [Results](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/metadata-clock-final-r21-results.json)
are 11.808 wall/11.684 CPU and 13.913 wall/13.850 CPU seconds, both below the
20-second test bound; [build commands](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/metadata-clock-final-r21-build-commands.json)
bind the private executables. Workspace is 400,020 bytes, eight more for source
identity. The [earlier 24-case pass](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/metadata-clock-tests-candidate-r21.log)
includes duplicate deliveries and remains separate from this unique-dwell check.
This candidate is included in the R22 normal build and the wide-Live check now
passes above. Its private component bindings remain distinct; no additional
full-Live phase/filter-delay certification follows, and original R20 failures remain.

The [R16 full GUI group](../local-evidence/helper-two-tone/r16-gui-group.log)
was **38/39**, failing the obsolete `Independent-hop` label expectation;
selected [R17 full Planner](../local-evidence/helper-two-tone/r17-planner-full.log)
and helper-only repairs passed; the later 39/39 result above supersedes this failure. The R15
[focused math group](../local-evidence/helper-two-tone/r15-components.log)
passes 8/8, [rolling](../local-evidence/helper-two-tone/r15-rolling-helper.log)
passes withdrawal/drain/post-loss join, and R16
[GPS/Default/final-loss](../local-evidence/helper-two-tone/r16-live-clock-loss-detail.log)
passes 3/3. R17 [actual-RF/loss/rolling/grid-union/full-frequency-window](../local-evidence/helper-two-tone/r17-focused-runtime.log)
checks pass while retaining the original selector-cache punctuation-fixture failure.
The migrated R19 [joint-clock/query/selector-cache checks](../local-evidence/helper-two-tone/r19-joint-clock-runtime.log)
now pass 3/3 (36.93/0.01/3.04 seconds). [Detailed output](../local-evidence/helper-two-tone/r19-joint-clock-runtime-detail.log)
and [fixture pins](../local-evidence/helper-two-tone/r19-probe-inputs.sha256)
(`test_helper_joint_clock.cpp` `0d0e3ae1…`) bind the repair: dated qualified
ToneTracker pilot mapping follows Live, while a separate auxiliary receiver
supplies 14 markers. The fixture no longer depends on legacy Receiver frequency
from the unpunctuated pure pilot. Original failed results remain preserved.

The [R15 HF40 run](../local-evidence/helper-two-tone/hf40-r15-preserved/run.log)
completes **public exact reception** (exit 0): eight source bits, pending identity,
exact prefixes and fully scored END; 1,193,132 logical samples/194.956 audio
seconds, 80,304 FFT jobs/306 input transforms across 208 owners, peak DSP
667,434,466 bytes under the fixed 2 GiB grant and no health drops/resets.
Fixture CPU/wall is 334.369904/334.794291 seconds; external
[usage](../local-evidence/helper-two-tone/hf40-r15-preserved/rusage.json) is
334.492782 CPU/334.941396 wall. [Exact launch and source pins](../local-evidence/helper-two-tone/hf40-r15-preserved/launch.json)
bind the nonhopping R15 binary, not later R16 Live changes:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 360 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf40-r15-preserved
```

The larger wall allowance changes no transmitted airtime and permits final
Simulation replay/public delivery. One invalid runtime join and seven unfinished
reads make granular latency/backlog unavailable. The 319-second replay-publication
interval is not unprocessed receiver backlog. The preceding R14 deadline and
HF15k false `01` failures remain independent; this run supplies no matched CPU
speedup or actual-retune acceptance.

The R16 [actual independent-clock Session](../local-evidence/helper-two-tone/live-oscillator/session-r16-proof.log)
now passes its extended-arithmetic constructor clock-cell proof for all 32
physical geometries, retaining exact `001`, stable identity/END, 71 model updates
and 1,582 corrections. The [original failed proof](../local-evidence/helper-two-tone/live-oscillator/session-r16.log)
and its [source pins](../local-evidence/helper-two-tone/live-oscillator/session-r16.sha256)
remain, separate from [reviewed proof pins](../local-evidence/helper-two-tone/live-oscillator/session-r16-proof.sha256.json).
Its 7,483 FFT jobs exceed the R14 baseline's 5,600, so this strong Session is no
work-gain comparison.

The [R17 selector repair](../local-evidence/helper-two-tone/selector-domain-r17.sha256)
(`684201ee…`, independently reviewed) clips LF cells to the original physical
domain even when clock qualification is absent. The strict 12-second SF512
[actual-retune prefix](../local-evidence/helper-two-tone/live-oscillator/work-window-r17.log)
passes: all 15 LF alternatives across ±300 ppm remain; 174/419 whole-prefix
mask snapshots narrow. At the [same 95,312 logical-sample frontier](../local-evidence/helper-two-tone/live-oscillator/work-window-r14-r17-common-frontier.json),
FFT jobs fall **315→224 (−28.9%)**, owners 9→12 and input transforms 1→2;
first FFT advances 11.514→10.714 audio seconds. Capture is the same 388,225
physical samples. There are 29 model updates/643 dated corrections across four
RFs with zero observed bound escapes, peak DSP 42,257,824 bytes and 12,800 raw
staging samples. Whole-process CPU instead rises 1.207813→1.276541 seconds
(+5.69%); differing trace/audit scope prevents a CPU-savings claim.
[Runtime/source pins](../local-evidence/helper-two-tone/live-oscillator/work-window-r17-runtime-source-binary.sha256)
and [usage](../local-evidence/helper-two-tone/live-oscillator/work-window-r17.rusage)
bind the new prefix. Pilot C/N0 target is sampled 40 dB-Hz; configured RF budget
is unmodeled. This 12-second observation supplies no payload-completion result.

The [dated continuous-Q discriminator](../local-evidence/helper-two-tone/continuous-q-review/run.log),
[source/binary pins](../local-evidence/helper-two-tone/continuous-q-review/source-binary.sha256)
and [commands](../local-evidence/helper-two-tone/continuous-q-review/commands.json)
resolve the current 48 escapes: one at startup (1.215875 s, error +0.05955 Hz/U
0.05902 Hz), then 47 after drift onset from 30.015875 to 30.935875 seconds,
maximum 82.31 times U, with zero loss-period escapes. CUSUM resets at 30.955875.
The piecewise 0→0.03 Hz/s control is phase/frequency continuous, phase noise zero
and inside S=0.5. A **LAB-only** frequency-rate PSD experiment gives:

| Q (Hz²/s³) | Post-onset escapes | Final constant-tone U (Hz) |
| --- | ---: | ---: |
| 0 | 47 | 0.000207 |
| 0.00001 | 60 | 0.01098 |
| 0.0001 | 0 | 0.01961 |
| 0.0009 | 0 | 0.03434 |

Q=0.0009 has zero total escapes/resets and 4,380/4,800 qualified polls in this
bounded control. These diagnostic variants omit finite-Hann R/C terms and retain
the old backward query; they are not a production-Q pass or drift calibration.
The proper R22 process/Hann R-C covariance, phase initialization and retained-age/
reverse-RMS queries pass a
[16-pair matched component ensemble](../local-evidence/helper-two-tone/continuous-process-r22/runtime/summary.json)
(30/40 dB-Hz, four seeds, constant/late drift). Positive Q has zero post-startup
escapes over 33,545 current estimates and zero escapes over 34,623 backward
0.4-second queries; two startup escapes remain (max error/U 1.00256). Q=0 retains
457/33,405 current and 296/34,477 dated escapes plus four startup escapes. All
32 variants end absent/unqualified while retaining dated knowledge. Positive-Q
final signal-observation U is ±0.03265–0.06052 Hz, availability 97.61–98.52%,
storage 766,656 bytes and normal component DSP CPU 0.187356 seconds/768 audio
seconds. The earlier private run records 0.243919; these are component timings,
not total-process gains or calibrated confidence.
[Source/binary pins](../local-evidence/helper-two-tone/continuous-process-r22/runtime/source-binary.sha256),
[commands](../local-evidence/helper-two-tone/continuous-process-r22/runtime/commands.json)
and [runtime](../local-evidence/helper-two-tone/continuous-process-r22/runtime/run-current.log)
bind the measured test `65f46c6f…`; final `7bcc6601…` adds empirical ≥99% sentinels/
comments. The later normal continuous check above executes these sentinels; its
exact final source/binary seal remains pending. The dense process/Hann oracle has
max relative error 2.503e−8; positive-Q phase-gap/S=0 covariance checks pass.
The original [final-noise authority failure](../local-evidence/helper-two-tone/continuous-process-r22/runtime/run-r1-valid.log)
remains; its old binary was overwritten by a scratch ownership collision, so no
fully bound pre-fix matrix is claimed. Only independent-frequency/coarse witness
renews the existing 0.4-second lease; weak continuous phase does not renew it.

The [private forecast check](../local-evidence/helper-two-tone/continuous-process-r22/forecast/forecast-result.json)
passes (448 bytes/0.071 CPU seconds), with all Q=0 numeric fields unchanged in
[24 controls](../local-evidence/helper-two-tone/continuous-process-r22/forecast/zero-control-assessment.json).
[Forecast pins/commands](../local-evidence/helper-two-tone/continuous-process-r22/forecast/forecast-source-binary.sha256)
retain the separate algebra scope; the [missing-include syntax failure](../local-evidence/helper-two-tone/continuous-process-r22/forecast/gui-syntax-missing-include-failed.log)
and scratch-collision invalid rerun remain excluded from passing evidence.
Explicit HF assumptions `1e-6 RF ppm²/s³` and `0.01 source ppm²/s³` are operational
model assumptions, not calibrated drift or physical floors; GHz cases do not
inherit HF Q. The independent-retune constant-drift/Brownian comparison excludes
operational RF/audio acceleration, retune scatter and integrated-model precision;
it selects neither TX lead nor receiver-cost precision. This is a string/comment
scope correction, not a measured retune-precision forecast. Earlier failures
and Live acceptance limits remain.

The [source-bound adaptive-factory consumer](../local-evidence/helper-two-tone/oscillator-model/live-adapter/r16-full-consumer/consumer.log)
passes 236 frequency fits, including 21 outside the earlier phase-qualified
subset, and 237 next-hop predictions with zero observed escapes. Each fit uses
384 ms within independently phased physical hops at 7/21/35/49 MHz; there is no
truth seed or phase/UTC authority. Model storage is 42,816 bytes and model CPU
about 0.0004 seconds per 32 audio seconds. Explicit constant-fractional-drift/Q
assumptions and nominal Hann-kernel approximation remain conditional and do not
calibrate hardware noise or erase earlier WLS/CUSUM failures. Separate
[input](../local-evidence/helper-two-tone/oscillator-model/live-adapter/r16-full-consumer/inputs.sha256),
[output pins](../local-evidence/helper-two-tone/oscillator-model/live-adapter/r16-full-consumer/outputs.sha256)
and [commands](../local-evidence/helper-two-tone/oscillator-model/live-adapter/r16-full-consumer/commands.txt)
bind these component results. Initial [permission-denied consumer setup](../local-evidence/helper-two-tone/oscillator-model/live-adapter/r16-full-consumer/consumer-setup-invalid.log)
and [missing-time Session setup](../local-evidence/helper-two-tone/live-oscillator/session-r16-setup-invalid.log)
are retained separately from algorithm behavior.

The joint covariance can mathematically project a DATA-frequency marginal, but
[explicit `data_frequency_valid`](../src/live_helper_oscillator.hpp) is preparation
only. A direct Helper aperture grants no joint DATA certificate; consumers also
need availability and dated support over the whole scored observation. A point
marginal cannot alone bound an entire long symbol. No new DATA-specific mask is
applied, and the preparatory field grants no timing/phase authority.

The full [R16 EME run](../local-evidence/helper-two-tone/eme-r16-full/run.log)
**fails** (exit 1): true TX epoch `1800000020`, one of eight canonical bits and
three one-bit rows, 1,791.731 audio seconds, 1,095.871896 CPU/1,084.523511 wall
seconds, 1,816 owners and 804,554,098 peak DSP bytes, with no health faults.
Native FFT jobs are zero; projected work is active, while compact lane counters
are inapplicable to this ordinary shared-pulse lattice. Neither zero lane counts
nor the first-window hypothesis explains away the failure: the first window is
not missed. The [exact launch/binary binding](../local-evidence/helper-two-tone/eme-r16-full/launch.json)
is:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --eme --helper on --helper-power-w 15000 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 1200 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/eme-r16-full
```

The [R17 shared-crystal HF15k primary](../local-evidence/helper-two-tone/hf-crystal15k-r17/run.log)
passes public exact eight bits/pending/END at actual +3 kHz/+100 ppm, but strict
row qualification **fails**: signal 4's raw `10` after canonical slots 3–4
prematurely completes at sample 460,517 versus true physical end 721,595 in the
[maintained trace](../local-evidence/helper-two-tone/hf-crystal15k-r17/hf-crystal-on-bits-18885035395649.jsonl).
Four other secondary rows are legitimate suffixes. No row merge or identity/END guard
relaxation is proposed. Cost is 53.440183 CPU/52.793840 wall seconds, 2,317 FFT
jobs/306 transforms, 207 owners and 653,881,725 peak DSP bytes. The fixed lab key,
2 GiB/CPU-2/3 [launch pins](../local-evidence/helper-two-tone/hf-crystal15k-r17/launch.json)
keep it distinct from older ephemeral-key runs:

```sh
taskset -c 2,3 build/dev/test_helper_normal_acquisition --hf-crystal --helper on --helper-power-w 15000 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 0ms --bit-count 8 --wall-seconds 360 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf-crystal15k-r17
```

The bounded [R19 HF continuation probe](../local-evidence/helper-two-tone/hf-crystal15k-r19-probe/run.log)
ends at its requested 700,000-logical-sample cutoff (exit 2); its
[launch/source binding](../local-evidence/helper-two-tone/hf-crystal15k-r19-probe/launch.json)
and [trace](../local-evidence/helper-two-tone/hf-crystal15k-r19-probe/hf-crystal-on-bits-19684116555044.jsonl)
retain the private-code-address divergence. The actual-key
[PatternCode/tuning control](../local-evidence/helper-two-tone/hf-native-batch/address-control/run.log),
[source/binary pins](../local-evidence/helper-two-tone/hf-native-batch/address-control/source-binary.sha256)
and [commands](../local-evidence/helper-two-tone/hf-native-batch/address-control/commands.json)
confirm it: partial epoch `1800000031`/phase 4760 indices 1–2 and canonical
epoch `1800000004`/phase 0 indices 3–4 share epochs `1800000046`/`1800000060`,
with both bit rows identical at all 2,560 chips
and fractional shaped samples (correlation 1). The next rows address epochs
74 versus 75; maximum raw/shaped/cross-bit correlation magnitude is 0.0351704.
This confirms a current-code address alias with correct six-second END duration. The
component control replays no PCM/acquisition and changes no identity or END
contract. The strict row failure remains open, without a receiver fix or pass.
The accepted diagnosis of the retained native window is stronger than a
presentation/source-row issue: the wrong calendar alias fully scores absent
address `1800000074` while true address `1800000075` is strong in the same
window. Scored duration is sufficient, but absence for the wrong code address
does not establish physical END for the transmitted source. This is an
**END-authority defect**, not a duration shortfall. The
[retained native receipts](../local-evidence/helper-two-tone/hf15k-terminal-join-r1/retained-native-input.json)
also support a private terminal-join component investigation; that prototype is
not a Live receiver fix or strict acceptance pass. No source-identity/prefix
guard, framing or six-second physical-END requirement is relaxed.

The [R19 EME first-two-symbol probe](../local-evidence/helper-two-tone/eme-r19-probe/run.log)
ends at its requested 300-second media window (exit 2), with
[exact launch/source binding](../local-evidence/helper-two-tone/eme-r19-probe/launch.json).
At correct epoch `1800000020`, the [decision trace](../local-evidence/helper-two-tone/eme-r19-probe/eme-on-bits-19684114134101.jsonl)
fully scores all 768,000 samples of the first 128-second symbol, with
`helper_unobserved=0`, `helper_block_suppressed=0`, `can_start=1`: score
30.1203 is below threshold 31.0201. The second symbol scores 42.8810 above
33.2175 and is admitted. Both entire windows are frequency-certified (maximum U 0.000823733/0.000123512
Hz); phase correction covers 114.384/119.055 seconds. This rejects missed-startup/
support-gap explanations without establishing a physical SNR limit. Recorded
channel DATA C/N0 is modeled −4.97940009 dB-Hz, consistent with sampled SNR and
128-second energy 40.6696 linear; it is neither cable-measured nor a DATA-fit
measurement. RawFit/quarter/differential score decomposition remains unobserved,
so the remaining failure is unresolved. Opt-in `--receiver-probe`/trace additions
remain default off and change no DSP behavior.

The [matched first-symbol phase ON/OFF control](../local-evidence/helper-two-tone/record-frequency-integration-r1/eme-first-symbol/analysis.json)
uses the same geometry/constructor, covered raw PCM and frequency records.
At the canonical epoch/window, scores are **30.1203/30.0786**, both below the
same **31.0201** threshold; every fit has 768,000 samples, no unobserved/suppressed
Helper input and valid frequency coverage. ON has phase correction for 686,303
samples versus zero OFF. The PCM hash covers physical samples [0,4,000,000),
including canonical end 3,933,000; later tails/whole-run CPU lie outside that
proof. Phase gives a small benefit here, not a physical SNR floor, general gain
or complete eight-bit/END result. [Commands](../local-evidence/helper-two-tone/record-frequency-integration-r1/eme-first-symbol/commands.json)
and [private bindings](../local-evidence/helper-two-tone/record-frequency-integration-r1/eme-first-symbol/private-binding.json)
retain the control.

The [known-truth EME power component](../local-evidence/helper-two-tone/eme-data-power-component/run.stdout)
has DSSS factor 1 and **no Interleaved V2 backoff**. Nominal configured/sampled
DATA C/N0 is −4.979400 dB-Hz, whereas the actual keyed/gated symbol is
**−6.414161 dB-Hz, E/N0 29.227646 linear**. Actual-symbol power is 1.434761 dB
below its nominal reference; Fake-FHSS gating alone costs 1.591813 dB relative
to the actual ungated waveform. Measured generated-noise density differs by
0.001040 dB from its configured value. [Bindings](../local-evidence/helper-two-tone/eme-data-power-component/bindings.json)
and [commands](../local-evidence/helper-two-tone/eme-data-power-component/commands.json)
retain this finite waveform measurement. Its truth template/ideal AWGN projection
is not the actual ordinary score, acquisition, decode, cable measurement or
physical SNR-floor proof; no payload power/path-loss adjustment follows.
The [gate-template component discriminator](../local-evidence/helper-two-tone/eme-gate-template-discriminator/run.stdout)
uses the same generated PCM to compare the documented ungated receiver template
with that template multiplied by the known source gate. Guarded score improves
**17.9804→20.9997**. The ordinary compact candidate's original-template component
matches the scalar score to 5.85e−12, but this is truth-timed/generated PCM,
not the original Live noise/filter/phase realization or an acquisition result.
[Bindings](../local-evidence/helper-two-tone/eme-gate-template-discriminator/bindings.json)
retain the concrete template-efficiency opportunity. No production gate edit,
eight-bit/END or physical-floor claim follows. The completed frozen contract run
above does not promote this private experiment.
A private [shared-gate producer/selector component](../local-evidence/helper-two-tone/eme-gate-template-discriminator/shared-gate-r1/analysis.json)
passes 12 numerical geometries, with maximum score error 7.59e−13 and quarter
error 1.55e−12 against its scalar controls. Its repaired runtime uses 8.99483
CPU seconds. The original setup and signed-shift audit failures remain retained.
This is not full-correlator family integration, Live acquisition, physical END
or a promoted production gate; its proposed integration remains private.
A later [copied actual-correlator component](../local-evidence/helper-two-tone/eme-gate-template-discriminator/shared-gate-r1/correlator-r1/analysis.json)
checks nominal/split, unsupported, full-zero and absence behavior. Nominal
guarded score is 17.980448→20.306594 after charging the extra-family `ln(2)`;
storage is 24,856 bytes/lattice. The strong scalar absolute-1e−8 comparisons
**fail**, retaining separate errors +3.53e−7 for `q` and +9.72e−6 for `q*g`.
Functional controls do not erase those numerical failures. Reviewed native-export
R2 remains unexecuted/unwired. A later
[private O3 Live gate experiment](../local-evidence/helper-two-tone/eme-gate-template-discriminator/shared-gate-r1/correlator-r1/live-O3-r1/analysis.json)
fails at its CPU limit (`SIGXCPU`, 145.868 process CPU seconds) before the
canonical first-symbol checkpoint. Required PCM hash/fence and canonical gate
score are unavailable. Its eight ordinary work frontiers remain at sample
681,750, with 144 owners and about 91.31 MB additional optional projector storage.
That prototype already uses eight active-lattice shared projectors, not one per
arrival; new "sharing" is not an untested cure. The private alternative replacing
33 × Moments prefixes with streaming segments has a limited private component
acceptance (17.64% producer CPU and 60% storage reduction). The later closure
compile/oracle failure above retains its own binding; no Live acquisition or
receiver-completion claim follows.
The matching 5,640 frequency-trace records establish trace identity, not PCM
identity or a matched whole-work CPU comparison. No complete eight-bit/END,
physical SNR floor, production promotion or broad Live qualification follows.

An [actual auxiliary phase control](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/run.log)
retains zero markers in the requested noisy pilot/auxiliary/DATA mixture. With
AWGN/phase noise zeroed, [DATA-on control](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/zero-control.log)
has nine markers versus 12 muted through six seconds. The first post-DATA
reversal at 4.8319 seconds passes plateau/raw-boundary checks but has normalized
residual 0.000525–0.000782, above the 0.0002 gate, versus 0.000002089 muted.
The [noisy Helper-only lead](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/gate-noisy-lead.log)
never establishes coarse validity. [Control pins](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/gate-probe-inputs.sha256)
retain these declines; optional phase certification remains unqualified.
A bounded [raw-LS likelihood control](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/)
classifies 13 punctuations/13 boundaries with zero wrong edges and no straight-
tone null detections, including three post-DATA edges. Likelihood ratios are
442.20–591.39, but curvature coverage is only 11/13 and uncalibrated. The causal
availability upper bound is 54.95 ms, not an observed 39–44 ms latency; cost is
about 1.3 CPU seconds/six audio seconds. A subsequent
[bounded-search rematch](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/profile-repair/results.json)
retains 13/13 punctuations/boundaries and zero straight nulls, with 13/13 curvature
coverage, timing RMS 0.230 ms/max 0.450 ms and actual release 40.8–52.5 ms in this
single case. [Source pins](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/profile-repair/inputs.sha256)
retain it separately from the original clipped-search failures. The private
[six-seed ensemble](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/ensemble-six-seeds/analysis.json)
detects 80/81 eligible punctuation edges with 79/80 conditional curvature
coverage, one escape and one miss, with zero null/wrong-edge detections.
[Runtime results](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/ensemble-six-seeds/results.json)
use 15.69 CPU seconds. This is not calibrated operational coverage or authority
integration; all misses, escapes and original clipped-search failures remain.
The subsequent [guarded producer matrix](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/guards-matrix/analysis.json)
**fails** over 14 captures/20.576 CPU seconds: all 178 candidates decline and
all 81 normal eligible markers are missed, with zero qualified publications.
The straight-tone competitor remains unconverged after two clipped Gauss–Newton
steps (remaining information norm 17.588–29.458 versus the 0.01 gate), despite
157/157 punctuation and 152/157 boundary own-fit convergence. No publications
occur on null/fade/loss controls either; zero false outputs here do not qualify
detection. The earlier one-seed seven-parameter coverage is not producer
qualification. The private branch remains unpromoted, with no physical SNR-limit
claim; [runtime results](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/guards-matrix/results.json)
retain every decline.
A private [null-variable-projection follow-up](../local-evidence/helper-two-tone/hf-native-batch/auxiliary-phase-control/likelihood-r23/null-variable-projection/matrix/analysis.json)
qualifies/covers 75/81 normal markers and 77/77 boundaries, with six marker misses,
zero accepted-interval escapes and zero straight-tone publications. The fade/loss
case covers 10/11 eligible markers and nine boundaries, with one miss and no
post-loss publication; stale-retention failure was not exercised. All 14 captures
complete in 30.095 process CPU seconds. Normal process cost averages 0.4837
CPU/audio, including 0.2829 for the marker path; these private scopes are not
integrated Live cost. The experiment pins the earlier `3a7b90ec…` archive.
Conditional likelihood/coverage, six misses and source limits remain; no Live
timing authority or confidence calibration follows.

The [ideal-frequency/truth-timing EME ceiling](../local-evidence/helper-two-tone/eme-r19-probe/ideal-frequency-ceiling/run-eight-bit.log)
is explicitly a laboratory ceiling, with [source/binary pins](../local-evidence/helper-two-tone/eme-r19-probe/ideal-frequency-ceiling/source-binary.sha256)
and [commands](../local-evidence/helper-two-tone/eme-r19-probe/ideal-frequency-ceiling/commands.json).
Its eight truth-timing effective scores are
`[34.15,46.79,21.53,17.45,22.09,66.01,15.61,27.41]`; several remain below the
actual Live charged thresholds. It does not receive all eight bits even with
exact CFO/truth timing (physical masked wire `00000100`, public source
`11010110`). Its pipeline/PCM is not bitwise matched to Live, so it establishes
neither a physical SNR floor nor Live gain, and currently justifies no fine-
arrival change. Acquisition proof and receiver timing authority are absent.

The [R16 contract build](../local-evidence/helper-two-tone/r16-contract.log)
and seven-job [R19 attempt](../local-evidence/helper-two-tone/r19-contract.log)
were interrupted/reaped 130 before tests. The
[R19 four-job continuation](../local-evidence/helper-two-tone/r19-contract-4jobs.log)
then fails compilation on obsolete startup-guide `anchor_start`/`anchor_index`
fields. The independently reviewed test-only active-union/prefix-sentinel
migration (`4ba034…`) [passes focused runtime](../local-evidence/helper-two-tone/r19-startup-guide-runtime.log)
in 0.56 seconds. R20 [all contract prerequisites compile](../local-evidence/helper-two-tone/r20-contract-build.log).
The full `./build.sh test contract` [runtime](../local-evidence/helper-two-tone/r20-contract-runtime.log)
is **INCOMPLETE**: interrupted/reaped 130 after about 24 minutes in the first
serial differential-probability test, without a completed runtime result. It is
not running or passed. The headless configuration registers zero tests in the
normal native group, so native coverage is unavailable, not passed. Focused
[ASan/UBSan results](../local-evidence/helper-two-tone/continuous-process-r22/dated-frequency-asan-ubsan-results.json)
pass 3/3 on pre-index core `9e844a2d…`: continuous/protocol/native implicit at
2.6205/0.1143/0.3149 seconds. All [three initial leak-enabled runs](../local-evidence/helper-two-tone/continuous-process-r22/dated-frequency-sanitize-results.json)
exit 1 with `LeakSanitizer does not work under ptrace`; only the explicit rerun
appends `ASAN_OPTIONS=detect_leaks=0`. Leak coverage is unavailable. Separate
private exact-index [translator ASan/UBSan controls](../local-evidence/helper-two-tone/continuous-process-r22/translator-index-cpu/sanitizer/results.json)
pass with bitwise output/state equality, no findings and 14.20 process CPU
seconds. [Filter ASan/UBSan controls](../local-evidence/helper-two-tone/continuous-process-r22/filter-index-cpu/sanitize/)
pass all 32 bitwise cases in 5.04 CPU seconds. Both explicitly disable leak
detection; these bounded checks do not constitute a full sanitizer-stage pass. Full contract/
1,024-byte/large-payload, cable and intended-hardware qualification
remain open.

### R14 runtime and frozen R15 changes

The [R14 GUI build](../local-evidence/helper-two-tone/r14-app-build.log) and
[shared self-check](../local-evidence/helper-two-tone/r14-gui-self-check.log)
pass. [Binary pins](../local-evidence/helper-two-tone/r14-app-binary.sha256)
are GUI `c87d78d2…` and core archive `68f79b15…`. Its
[six-test focused group](../local-evidence/helper-two-tone/r14-components.log)
is 5/6: only Planner forecast fails. The [numeric discriminator](../local-evidence/helper-two-tone/forecast-weak-r14/diagnostic.log)
confirms TX/Planner agree on the 36-second cap and decline U/RF authority; only
required reason text is missing. The [reason-only repair](../local-evidence/helper-two-tone/forecast-weak-r14/repaired-header.log)
is independently approved, awaiting a current rerun. Both fresh unchanged
[GPS and Default-clock controls](../local-evidence/helper-two-tone/r14-live-clock.log)
pass (2/2, 8.12 seconds).

The [R14 rolling check](../local-evidence/helper-two-tone/r14-rolling-helper.log)
still fails `post_loss_join`, although processed-frontier repair now works and
`drained=1`. Its [dated failure analysis](../local-evidence/helper-two-tone/rolling-frontier-r14/finding.txt)
shows the stop fence `143418+24000=167418` preceded the first unlocked interval's
required `144618+24000=168618`. A stronger wait through observed withdrawal's
capture date plus three seconds is reviewed, pending rerun. The separate
[valid final-Helper-loss run](../local-evidence/helper-two-tone/session-helper-loss/session-loss-r14-summary.json)
retains exact `001`, stable signal 2, accepted post-loss wire progress and fully
scored physical END without health drops. It fails an unsuitable new-acquisition
job-count requirement while established tracking/scoring continues. The reviewed
test repair requires the same source's post-loss wire bit and END; it has no
new runtime result yet. The ABI-invalid earlier loss attempt remains below.

The [R14 full HF40 attempt](../local-evidence/helper-two-tone/hf40-r14-current/run.log)
reaches the 300-second deadline (exit 2): 179.583 captured audio seconds,
299.587 process CPU seconds, 72,805 FFT jobs/276 input transforms and 192 owners.
[Launch/source pins](../local-evidence/helper-two-tone/hf40-r14-current/launch.json)
and [process usage](../local-evidence/helper-two-tone/hf40-r14-current/rusage.json)
bind the exact command:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 300 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf40-r14-current
```

Internally, the canonical source is exact `11010110` (wire `10110100` before
XOR decoding), with pending identity, fully scored END and all prefix/identity
checks; four completed suffix rows have legitimate distinct births. Public GUI
publication remains unqualified: Simulation wave/replay was not installed before
the deadline, so public `exact=0` does not identify a wrong payload or row-identity
failure in this run. The older HF15k false `01` result is separate and remains a
strict failure. No full HF GUI acceptance or matched throughput claim follows.

The R14 [actual-retune independent-clock baseline](../local-evidence/helper-two-tone/live-oscillator/baseline-r14.log)
passes exact payload/END across 73 segments with 349 qualified references and
zero frequency escapes. [Summed work](../local-evidence/helper-two-tone/live-oscillator/baseline-r14.metrics.json)
is 5,600 FFT jobs, not the single-owner peak 420; [usage](../local-evidence/helper-two-tone/live-oscillator/baseline-r14.rusage)
is 3.382 CPU seconds for 29.09 audio seconds. A separate 12-second SF512,
sampled-pilot-40-dB-Hz [physical work window](../local-evidence/helper-two-tone/live-oscillator/work-window-r14.log)
retains its original postprocessing proof failure. The [same-capture LF-slab/cell-union proof](../local-evidence/helper-two-tone/live-oscillator/work-window-r14-reanalysis.log)
passes after independent review: 315 actual FFT jobs, nine owners, 37/100 masks
narrowed, 147 qualified polls and zero frequency escapes, max error 0.154 Hz
against U 0.194–0.280 Hz. Its [support metrics](../local-evidence/helper-two-tone/live-oscillator/work-window-r14.support-metrics.json)
and separate [runtime](../local-evidence/helper-two-tone/live-oscillator/work-window-r14-runtime-source-binary.sha256)/[proof pins](../local-evidence/helper-two-tone/live-oscillator/work-window-r14-proof-source-binary.sha256)
retain that distinction; no new runtime, phase-error or full-payload claim follows.

The reviewed R15 phase-preservation change retains dated frequency/rate covariance
across a phase-only reset, initializing the new phase nuisance independently.
It no longer clears frequency merely after a 200 ms phase gap; presence loss still
withdraws authority, and local frequency does not require a qualified phase fit.
A conservative gap `S*T` enclosure is included. Eight
[matched 30/40 dB-Hz fade/jump controls](../local-evidence/helper-two-tone/frequency-preservation/matched-revised.log)
have no revised escapes; the [baseline's two escapes](../local-evidence/helper-two-tone/frequency-preservation/matched-baseline.log)
at 40 dB-Hz/1.3-second fade remain. That case improves first-return U
0.1743→0.00657 Hz and final U 0.00572→0.00312 Hz, with error 0.00122 Hz,
15.81 seconds of dated support and 6.67 seconds of current coherence. Component
cost is 0.00061–0.00076 CPU/audio, 766,576 bytes. [Final source pins](../local-evidence/helper-two-tone/frequency-preservation/final-r4-sources.sha256)
and [maintained check](../local-evidence/helper-two-tone/frequency-preservation/maintained-r4.log)
retain 48 aggregate escapes, now partitioned by the current discriminator above
as one startup and 47 after drift onset before reset. Earlier WLS failures remain. The piecewise drift is
0→0.03 Hz/s within the configured S=0.5 allowance, with zero phase noise and
continuous frequency/phase; no Q repair or physical limit is established.

The R15 actual-retune predictor is now implemented and independently reviewed,
[frozen separately](../local-evidence/helper-two-tone/oscillator-model/live-adapter/freeze-r1.sha256)
(Live `3a4f9747…`, adapter `ea2cb06f…`). It uses 384 ms usable fits and a joint
four-state capture-effective RF/common-source clock-and-rate covariance, explicit
HF ≤54 MHz Q assumptions, measured retune scatter and a six-sigma innovation veto.
Original B/UTC clock alternatives and Helper–DATA coupling remain; it grants no
phase or timestamp authority and does not reset on payload onset. Old-segment
seeds are causal without lookback state; one family receives a whole-block
frequency enclosure. Optional storage plus 64 KiB scratch is funded after mandatory
work; allocation/growth failure falls back. The stricter actual-model/date/error/
NCO/clock-edge/birth-U/FFT-sum test and two registered model/protocol tests await
runtime evidence. The [R15 app build](../local-evidence/helper-two-tone/r15-app-build.log)
and matched validation were pending at this checkpoint; later R15/R16 outcomes
are above. This source freeze alone is not a completed Live qualification.

### R13 integration and bounded component results

The [unchanged positive GPS-clock control](../local-evidence/helper-two-tone/aux-live-gps-r13.log)
passes after the optional-owner birth-resource repair: exact `001`, stable pending
identity, physical END, `clock=1` and `joined=1`, 24,857 jobs and 4.733 live CPU
seconds. The [executed command/result](../local-evidence/helper-two-tone/aux-live-gps-r13.time.json)
is `taskset -c 0,1 build/dev/test_helper_live_assistance --gps-clock-positive`
(exit 0, reaped), with [R13 input pins](../local-evidence/helper-two-tone/aux-live-gps-r13-inputs.sha256).
The earlier R12 failures remain below.

The unchanged Default-clock case initially fails: a competing same-birth source
hands off an explicit gap over an already exposed known prefix. The
[actual-source discriminator](../local-evidence/helper-two-tone/row-identity-review/r13_gap_discriminator.log)
and [counterfactual failure](../local-evidence/helper-two-tone/row-identity-review/test-gap-counterfactual-r1.log)
retain that defect. A narrow independently approved handoff repair passes the
[private same-Default CPU-0/1 check](../local-evidence/helper-two-tone/row-identity-review/gps-default-gap-r1-cpu01.log)
with exact `001`, stable pending identity, END, clock/joined assistance and 9,422
jobs; [private source/archive/binary pins](../local-evidence/helper-two-tone/row-identity-review/gap-r1-bindings.sha256)
keep this separate from the application build. The earlier
[rolling repair attempt](../local-evidence/helper-two-tone/helper-live-rolling-r13-repair.log)
still fails with processed frontier zero. The Simulation processed-frontier
repair is reviewed and [source-bound](../local-evidence/helper-two-tone/r14-frontier-source-binding.sha256),
had no runtime result at this checkpoint. The first dedicated Helper-loss attempt is
[SETUP_INVALID](../local-evidence/helper-two-tone/session-helper-loss/setup-invalid-r3.txt):
R13 header/trace-layout ABI mixing stalled before Helper/work observations;
its exact run was interrupted and reaped with exit 130. It provides no loss,
drain or completion coverage. Later R14 application/runtime outcomes are above.

The [short R12 EME continuous window](../local-evidence/helper-two-tone/eme-r12-continuous-window.log)
is incomplete (requested observation window, exit 2, reaped): 577,136 captured
samples/96.189333 audio seconds, 51.774258 CPU seconds and zero bits/END. Its final pilot U is 0.000189456 Hz over 96.02 seconds
of support. Existing admitted owners actually narrow 33→3 RF/clock pairs,
retaining three clock alternatives. This observation ends before a 128-second
four-symbol batch and cannot establish compact payload completion. Nonzero
compact work exists even where `compact_supported=0`; the original collector's
zero summary is a diagnostic failure, not absent execution. The corrected
[actual-component check](../local-evidence/helper-two-tone/work-trace-sampling/component-r2.log)
and [16-second trace-sampling replay](../local-evidence/helper-two-tone/work-trace-sampling/replay-r2.log)
preserve terminal maxima for all 121 numeric owners, including forced/retired
rows: 112,906,207 projected carrier samples, 516,619,008 skipped carrier samples
and 7,278,352,510 skipped lane samples. Original 31,624 work rows become 1,054
periodic rows without omitted terminal counters; [pins](../local-evidence/helper-two-tone/work-trace-sampling/inputs-r2.sha256)
bind the replay. These compact samples are not FFT jobs or a completed-receiver
speedup. The actual short-window launch was:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --eme --helper on --helper-power-w 15000 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 120 --observe-samples 576000 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/eme-r12-continuous-window
```

The [shifted-calendar full-dwell comparison](../local-evidence/full-dwell/integration/calendar-r2.log)
retains 44 raw fits with zero interval escapes using 384 ms settled apertures;
[final bindings](../local-evidence/full-dwell/integration/final-binding.sha256)
separate this component work from the pending GUI rebuild. The
[Hann/OLS kernel control](../local-evidence/full-dwell/kernel-r4.log)
checks CW/chirp and large sample coordinates, but an exact amplitude-aware
kernel is not proved. The reviewed four-state RF/audio frequency-and-rate model
is **component only**: its
[phase-qualified R4-producer subset](../local-evidence/helper-two-tone/oscillator-model/full-dwell/full-model-r3.log)
has 373 fits and zero raw/endpoint/next-hop escapes, with
[producer/model pins](../local-evidence/helper-two-tone/oscillator-model/full-dwell/inputs-r3.sha256).
New raw-frequency-only admissions are not yet validated. Positive process noise
is uncalibrated; [saved-row covariance replay](../local-evidence/helper-two-tone/oscillator-model/full-dwell/covariance-r2.log)
still gives about ±0.103–0.110 Hz at 49 MHz/40 dB-Hz in its positive-Q cases.
Neither this model nor the longer aperture erases the continuous WLS coverage
failures below.

A private [dated 25-seed milestone control](../local-evidence/helper-two-tone/forecast-milestone/result-dates-r2.log)
finds all 25 cases qualified and contained at a proposed four-second lead,
versus the old 38-second forecast. It also preserves eight escaped earlier
dated estimates. Sampled pilot C/N0 is 23.54 dB-Hz versus RF-budget 29.56 dB-Hz;
there is no ordinary bank in this control. These private results precede the
R14 integrated forecast check and grant no runtime authority or confidence calibration.

### R12 same-key GPSDO control and failed integration checks

The [R12 same-laboratory-key GPSDO/helper-OFF control](../local-evidence/helper-two-tone/hf40-r12-gpsdo-matched.log)
passes (exit 0, reaped): exact eight bits, pending identity and physical END,
996,080 samples/162.75817 audio seconds, 33.821037 process CPU seconds/33.61561
wall seconds, 5,103 FFT jobs/243 input transforms and 176 owners, peak DSP
660,571,797 bytes, with no health drops/resets. Exact fixture command:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf --gpsdo-only --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 120 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf40-r12-gpsdo-matched
```

[Normal-fixture/archive pins](../local-evidence/helper-two-tone/r12-normal-gpsdo-binding.sha256)
and [app/focused-source pins](../local-evidence/helper-two-tone/r12-app-focused-binding.sha256)
retain this control. Key/seed, 2 GiB workspace and available CPUs match the Helper
comparison, but GPSDO TX epoch is `1800000000` versus Helper `1800000040` because
of Helper lead. This is not the same dated wire or an exact causal CPU comparison.

The [full R12 helper-live attempt](../local-evidence/helper-two-tone/aux-live-r12.run.log)
fails after passing strong callsign, same-ID and clear-state checks: its rolling
lifecycle still requires obsolete broad-audit/fallback behavior. The later
current-policy rolling attempt remains failed as recorded above. The
[original setup failure](../local-evidence/helper-two-tone/aux-live-r12.log)
(`/usr/bin/time: No such file or directory`, exit 127) remains separate.
The [positive GPS clock run](../local-evidence/helper-two-tone/aux-live-gps-r12.log)
and [expanded-trace run](../local-evidence/helper-two-tone/aux-live-gps-expanded-r12.log)
both fail (exit 1): exact `001`, pending identity and END pass, but `clock=0`
and `joined=0`. Expanded tracing identifies 15 optional auxiliary owners reclaimed
at birth before four punctuation markers can accumulate. Payload correctness is
not a clock-assistance pass.

The narrow two-stage constructor-retention and post-commit NEW-owner union fix
is independently reviewed, with [source hash](../local-evidence/helper-two-tone/aux-live-constructor-repair-r13.sha256)
`c7593840755e3ad8f39f6d7d39a35f7ba0a9d6d23717e5be23e365033ba08dd2`.
The [R13 application build](../local-evidence/helper-two-tone/integrated-r13-app-build.log)
and positive-clock outcome are recorded above; they do not convert these R12
failed attempts into passes.

The bounded [aligned KF/WLS discriminator](../local-evidence/helper-two-tone/continuous-wls-r1/exact-match.log)
finds no arithmetic discrepancy in its matched states/covariance; known-noise
and known-ramp weights do not remove the coverage failures. Six seeded current
publication escape counts remain `[507,4,2,0,24,2]` in the
[retained analysis](../local-evidence/helper-two-tone/continuous-wls-r1/analysis-aligned.log),
with [frozen R5 bindings](../local-evidence/helper-two-tone/continuous-wls-r1/binding.sha256).
The production R5 estimator is unchanged. No multiplier or operational-confidence
claim is introduced.

### R12 build and completed R11 full HF result

The [R12 application build](../local-evidence/helper-two-tone/integrated-r12-app-build.log)
finishes successfully with reaped processes. It includes the current metadata-
clock API and frozen Live integration source. The [focused-target build](../local-evidence/helper-two-tone/integrated-r12-focused-build.log)
also completes; its failed full helper-live/positive-clock runtime attempts are
recorded above. No full R12 integration or clock-routing pass is claimed.
These newer sources do not rebind the already-built R11 HF receiver below.

The [R11 full HF40 run](../local-evidence/helper-two-tone/hf40-r11-full.log)
is **incomplete** (exit 2, reaped): 172.0835 captured audio seconds,
299.730456 process CPU seconds/300.181 wall seconds and no physical END. True TX
epoch `1800000040` owner 48 has seven wire bits. The matching eight-bit primary
owner 33 is epoch 25, not true-TX completion; correct displayed bits do not make
this source-identity concern pass. No row merge is proposed.

Last committed input is 171.3368 seconds, 0.7467 seconds behind captured input.
The true owner's fourth-batch threshold is 173.4794 bank seconds, about 2.1426
bank seconds beyond committed input and approximately 1.808 audio seconds beyond
captured input, plus alignment. The primary owner's next threshold is 189.7067
seconds. These dated work thresholds explain incomplete support; they do not
promise correct completion or END merely by adding input.

Ordinary feed uses 294.117728 CPU seconds versus source generation 5.126036.
There are 70,370 FFT pair jobs, 263 input FFTs and 139 FFT-executing owners,
all with observed worker-slot peak 1. Most are already 35 pairs/35 clock
representatives, one RF per LF cell. Existing RF-mask shrink cannot remove those
required clock cells. Ordinary tabulated-FP64 component work is being
investigated; no optimization edit or benefit is claimed.

The [current metadata-clock component](../local-evidence/helper-two-tone/metadata-clock-two-tone-r12.log)
and [unchanged synchronous CLI regression](../local-evidence/helper-two-tone/metadata-clock-cli-r12.log)
pass (CLI 58.615 seconds), with
[production pins](../local-evidence/helper-two-tone/metadata-clock-production-hashes-r12.log)
and [CLI input pins](../local-evidence/helper-two-tone/metadata-clock-cli-input-hashes-r12.log).
The prepaid reservation is now 400,012 bytes at 48 kHz, up to 1.68 seconds total
raw PCM; a 96 kHz request above the optional budget declines metadata only.
Actual auxiliary punctuation exports a conditional constant-clock interval:
±550 ppm resampler controls retain truth in roughly 21 ppm-wide intervals from
11 markers each, with metadata disabled. Their nominal ±20 Hz discovery does not
prove +3 kHz auxiliary-clock acquisition without valid recentering. The pure
pilot has no punctuation clock, and auxiliary clock export grants no frequency,
phase or UTC authority. Component/API passes are not Live-routing acceptance.

### R11 build, finalized continuous evidence and remaining failures

The [R11 application build](../local-evidence/helper-two-tone/integrated-r11-app-build.log)
and [seven-target focused build](../local-evidence/helper-two-tone/integrated-r11-focus-build.log)
finish successfully with reaped processes. Application/source and test-binary
bindings are [recorded separately](../local-evidence/helper-two-tone/r11-app-binaries-sources.sha256)
from the [focused binaries](../local-evidence/helper-two-tone/r11-focused-binaries.sha256).
The [R10 ABI-consistent fixed-RF/independent-RF/Planner checks](../local-evidence/helper-two-tone/focused-r10-abi.log)
pass 3/3 in 6.88 seconds. The later [seven runtime checks](../local-evidence/helper-two-tone/focused-r11.log)
pass 7/7 in 19.01 seconds with reaped processes. These focused passes do not
establish uncertainty coverage or a final full regression pass.

The [final actual-channel comparison](../local-evidence/helper-two-tone/actual-input-r10/channel-final.log)
uses the same Controller-derived 24,480 Hz HF stream over 55 seconds. The repair
reduces resets 32→1 (startup), retains 54.805 seconds of coherence and changes
final U 0.032722→0.001678 Hz; final error is −0.000307 Hz with zero escapes over
1,948 unique published dates in this control. At the posthoc payload boundary
39.344 seconds, it retains a qualified dated U 0.002359 Hz from a 38.872-second
aperture, without a known receiver TX timestamp or payload-onset reset. Tracker
storage is 627,616 bytes with no raw history; about 0.0018 tracker CPU/audio is
contended component cost, not receiver throughput qualification.

The repair skips uninformative observations once the prior is resolved without
advancing support; contradictions, capture gaps and CUSUM changes still reset.
A reset clears old frequency records and rejects a context crossing that reset.
A noisy context endpoint retains the prior qualified dated publication instead
of replacing it with broader pooled uncertainty. Independent source review finds
no coding blocker, but this is not operational uncertainty coverage.

**Coverage failure is substantial.** The
[final controls](../local-evidence/helper-two-tone/continuous-final-controls.log),
[pinned to the R5 repair](../local-evidence/helper-two-tone/continuous-final-r5-binding.sha256),
retain a 40 dB-Hz, seed-1 constant-drift-from-start case with 507/2,967 escaped
unique current publication dates (17.1%). The separate 497/2,966 counters are
historical queries 0.4 seconds earlier, not publication deduplication.
Maximum error is about 3.97 sigma, mostly one 38–48.65-second excursion plus
12 early escapes. The older R5 drift-change control has 47 aggregate escaped
publications: later review partitions three at startup and 44 after drift onset;
it does not establish that all occur before detection. The later current 48-date
discriminator above resolves one startup and 47 post-onset escapes before reset.
These are not rare escapes. Actual-support/weighted-WLS diagnosis remains active; no multiplier or
operational-confidence claim is substituted for that work.

The [RF-drift transport check](../local-evidence/helper-two-tone/rf-drift/transport-r4-labelled.log)
uses USB/LSB and signed ±3e−9/s RF fractional slew. Zero slew is reviewed
byte-identical, including AWGN/audio behavior. Each strong 60 dB-Hz case has
19 fitted hops and zero endpoint escapes, but injects 0.5°/sqrt(s) source phase
noise while tracker phase-noise configuration is zero: it is explicitly an
unmodeled-noise stress control. Full-dwell and persistent-drift qualification
remain open.

The reviewed optional metadata API is integrated **after** the R11 build:
[production source pins](../local-evidence/helper-two-tone/metadata-api-production-hashes-r11.log)
and [private concurrency/geometry checks](../local-evidence/helper-two-tone/metadata-api-private-two-tone-r9.log)
retain the split capture/immutable-snapshot design, up to 1.68 seconds total raw
PCM and a 399,884-byte reservation at 48 kHz. The old R11 app is stale for this
API. Live integration is underway, not passed. A 96 kHz request exceeding the
default optional budget must decline metadata rather than payload functionality.

The completed incomplete R11 run above used this fixed laboratory-key fixture
command:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 300 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf40-r11-full > local-evidence/helper-two-tone/hf40-r11-full.log 2>&1
```

Later source edits cannot rebind that already-built binary or supply a result.

### R9 matched receiver work and current validation limits

The [matched R9 report](../local-evidence/helper-two-tone/hf40-matched-r9-summary.json)
compares the same current normal-fixture objects/archive, fresh test key, seed
5181, TX epoch `1800000040`, fixed 2 GiB workspace, CPUs 0/1 and 55.294118
captured audio seconds. The private control changes only the full tracker
constructor to `continuous=false`; [control pins](../local-evidence/helper-two-tone/continuous-control-r9/inputs.sha256)
retain that distinction, including the reconstructed exact R9 header SHA
`29cbf7047a201b9827c109f5ca249185a00b3b77b1f2f8259fea3ad0d4748a5b`.
Actual fixed-laboratory-key commands were:

```sh
taskset -c 0,1 local-evidence/helper-two-tone/continuous-control-r9/test-helper-normal-independent --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 180 --observe-samples 336600 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf40-matched-r9-independent > local-evidence/helper-two-tone/hf40-matched-r9-independent.log 2>&1
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 180 --observe-samples 336600 --keyfile /tmp/datapump-helper-matched-r9.key --evidence-dir local-evidence/helper-two-tone/hf40-matched-r9-continuous > local-evidence/helper-two-tone/hf40-matched-r9-continuous.log 2>&1
```

The requested 336,600-sample observation cut yields 338,400 actual captured
samples in both controls; that capture count remains distinct from processed
input or completed symbol coverage.

| Current tracker control | Process CPU (s) | Executed FFT jobs | Input transforms | Owners | Primary bits |
| --- | --- | --- | --- | --- | --- |
| Continuous disabled | 101.403089 | 46,536 | 22 | 67 | 0 |
| Continuous enabled | 78.041639 | 33,600 | 22 | 67 | 0 |

This is **23.04% less measured process CPU and 27.80% fewer executed FFT jobs**
in the matched window, not savings inferred from Hz width. Both controls have
one observed worker slot/22 executing owners, zero payload bits and no completion
claim. True-TX owner 47 still executes zero jobs; its initial catalogue is 275
pairs without continuous refinement versus 249 with it. The older R6-binary
supplementary run (100.577 CPU seconds) is not the causal baseline.
No health drops/resets/backlog are reported, but window stop interrupts one
checkpoint join: captured audio is not complete processed-frontier or symbol
coverage.

Qualified median U changes 0.270785→0.022499 Hz. That R9 24,480 Hz input has
frequent noise-based coherence resets, with U near 0.4 Hz at the end; the later
matched repair above addresses this software defect, not a physical ceiling. A separate
6,120 Hz component reaching U 0.00856 Hz does not establish current runtime
precision. Original/selected clock representatives stay equal within each owner:
9 for the disabled control, 9/35/37 for continuous. Both retain the full LF
domain; denser legal grids are not a clock-resolution gain. Later reviewed
history/CUSUM and batching repair evidence is separately bound above.

The [R9 application/source record](../local-evidence/helper-two-tone/r9-app-binaries-sources.sha256)
pins manual GUI `ec4511db77636863a1dc2f62f9c1dc50f9a30e856f60605991464e571b43c94d`
and archive `d9c7377f0b389b988bcea6425072b1a01ab57d6b259740ec9574c9932d122e19`.
Later public Planner-structure changes need ABI-consistent R10 rebuilding and
validation; this WIP is not final contract qualification. The
[private capture-wrapper setup](../local-evidence/helper-two-tone/actual-input-r10/capture.log)
exits 139 before pushing PCM, with
[archive pins](../local-evidence/helper-two-tone/actual-input-r10/capture-hashes.txt)
showing `530827e2…` rather than the R9 `d9c7377f…` archive. This is an identified
ABI/setup failure, not an estimator result. The
[debugger notice](../local-evidence/helper-two-tone/actual-input-r10/backtrace.log)
is `warning: ptrace: Operation not permitted`; startup exits 127. No permission
escalation or retry occurred, and all owned processes are reaped.

The [R9 focused group](../local-evidence/helper-two-tone/focused-r9.log) is **3/5**:
`helper_actual_rf_independent` fails its old further-RF-mask assertion and
`helper_tone_tracker` fails `pilot invented coherent aperture`. Both original
failures remain. The independent-clock Session otherwise retains exact `001`,
one END/four pending commits, 349 references with zero frequency escapes,
71 phase updates, 420 peak ordinary-job counter and 1,610 phase-covered-feed job
deltas. Split checks conserve 14,655 PCM samples and dates: mixed output is
three pieces (one qualified/two unsupported), fragmentation caps at eight, and
homogeneous continuous output stays one. These narrow passes are not a passing
full focused group.

The reviewed test-only mask criterion repair subsequently passes its bounded
R10 fixed/independent-RF checks above; the original R9 failure remains retained.
The [actual catalogues](../local-evidence/helper-two-tone/actual-rf-mask-diagnosis/catalogue-exact-clocks.log)
show unchanged-config ordinary constructor 437 pairs versus assisted 5, while
both cover the full ±300 ppm LF domain (legal clock grids 7 versus 5). Each
assisted owner is already born with one RF representative per LF cell, so further
in-owner RF shrink has zero opportunity and is **unexercised**, not passed.
The [complete-strip check](../local-evidence/helper-two-tone/actual-rf-mask-diagnosis/catalogue-full-strip.log)
retains measured-U endpoint coverage with worst clock radii 141.3955/146.2608 ppm
inside the advertised cells. No production RF prior, SNR or power changed.

The conditional [RF oscillator-model component](../local-evidence/helper-two-tone/oscillator-model/component-r10.log)
has no Live prediction model and assumes conditional Q=0. Signed next-hop
comparisons reach final U about 0.0416–0.0433 Hz at 40 dB-Hz and 0.132–0.166 Hz
at 30 dB-Hz, but unknown-jitter warm-up retains 96/608 escapes and later
25/5,760 escapes without a known floor. It is not operational qualification;
detailed model tables await final review.

### Current correction, R8 metadata and frozen local-RF evidence

The earlier 200 ms segmented contexts are a software aperture choice, not the
physical coherence of an uninterrupted non-hopping tone. Unknown phase after a
real retune still prohibits cross-hop coherent accumulation. A continuous-pilot
estimator is under implementation. Its bounded dated numeric retention may
exceed 36 seconds; that retention is distinct from the maximum **36 additional
seconds of transmitted precision lead**. No historical PCM search is introduced.

An untuned, matched 39-case continuous-estimator diagnostic shows a useful
constant-tone opportunity: at 64 seconds, provisional U is about
0.000389–0.000562 Hz at 30 dB-Hz and 0.000124–0.000130 Hz at 40 dB-Hz, versus
independent-aperture values about 0.0889–0.0943/0.0263–0.0281 Hz. This was
preliminary evidence; later coverage failures are substantial. Later finalized controls above establish substantial drift-case
coverage failure. These small radii are **not accepted operational precision**.
The older 0.22–0.25 Hz controls likewise describe short-aperture software, not a
physical continuous-tone limit.

The independently approved 24-line auxiliary raw-boundary witness uses the raw
zero-amplitude dwell boundary because the diagnostic notch distorts that timing
witness. Existing shaped-reversal/cadence checks remain. All eight frequency,
alignment and phase-jump geometries now decode seven metadata frames each in
[two-tone](../local-evidence/helper-two-tone/metadata-two-tone-raw-notch-r8.log)
and [full strong-receiver](../local-evidence/helper-two-tone/metadata-strong-raw-notch-r8.log)
checks. The unchanged CLI clear/encrypted metadata regression
[passes in 57.636 seconds](../local-evidence/helper-two-tone/metadata-cli-raw-notch-r8.log).
[Production source/archive pins](../local-evidence/helper-two-tone/metadata-raw-notch-production-hashes-r8.log)
and [CLI input pins](../local-evidence/helper-two-tone/metadata-cli-raw-notch-input-hashes-r8.log)
bind the tested archive
`14459138df74ef93b596444bb673a12d5853e365c578a5a5c9e82f7b6b9f7805`, not the
R7 archive. Earlier zero-frame failures remain individually retained. Live
optional metadata routing was missing in this R8 scope; current API/Live work is
separately recorded above and has no passing Live result. The existing GPS-clock
fixture failure is unchanged. Metadata never qualifies pilot-frequency assistance.

The [Live splitter source](../src/live_helper_frontend.hpp) and
[Session assertions](../tests/test_helper_actual_rf_session.cpp) were independently
reviewed/frozen. Their later R9 conservation checks and the still-failed
independent-mask assertion are separately recorded above; source acceptance
alone is not runtime or full-group qualification.

The separate frozen [local-RF diagnostic](../local-evidence/helper-two-tone/local-aperture/full-diagnostic.log)
covers 40 cases plus a drift control, with
[artifact pins](../local-evidence/helper-two-tone/local-aperture/r8-diagnostic-binaries.sha256).
At 30 dB-Hz, qualified-hop counts for seeds 1–8 are `[5,7,12,8,6,2,11,7]`;
seeds 1 and 6 fail required availability. Every tested endpoint/wrong-tone
counter is zero, but those two failures remain failures. Strong 40/60 dB-Hz,
wide-offset and phase-noise cases have 14–15 qualified hops; 20 dB-Hz cases
have none. Twelve ordinary plus
[three wide cold-noise seeds](../local-evidence/helper-two-tone/local-aperture/wide-noise.log)
produce zero qualifications. The diagnostic defers availability assertions while
counting the original failures; it is not a full runtime/matrix qualification or
physical SNR limit. Its frozen 403,072-byte tracker is not the changing
continuous-estimator workspace.

### Preceding R7/R8 focused checks and bounded diagnostics

The [source-band guard checks](../local-evidence/helper-two-tone/r6-source-band-tests.log)
pass 2/2, and the [strengthened actual-RF Session run](../local-evidence/helper-two-tone/actual-rf-session-source-band-r6.log)
passes: +2.19 Hz correction is admitted, +142.7 Hz declined with the old NCO
preserved. It retains exact `001`, one row/physical END and 69 phase updates;
3,626 owner-job deltas occur in phase-covered feeds. That measures covered feeds,
not whole-symbol phase coverage. [R7 focused checks](../local-evidence/helper-two-tone/focused-r7.log)
(actual-RF Session, pilot stage and tracker) pass 3/3 in 8.03 seconds.
The [main R7 build](../local-evidence/helper-two-tone/integrated-r7-build.log)
passes for GUI/CLI, bound by [R7 binaries](../local-evidence/helper-two-tone/r7-binaries.sha256):
GUI `a4f69b06bc7dcc38d94d3d47ad24915d693021129aba542627de501788c860d8`,
archive `0c8ad8b0cabfa2ddbbaf86655ed6d18644352212d16ed269d293194f923c874c`.
The later R8 metadata component/unchanged-CLI results above have separate archive
pins and do not establish a fresh complete GUI rebuild.

The reviewed covariance-based optional phase bounds change phase eligibility,
not frequency arithmetic. Matched 128-second **component** aperture scores include
all samples, with fixed physical RF and independent phase changes where stated:

| Pilot C/N0 | Phase calendar | Phase-corrected loss (dB) | Frequency-only loss (dB) | Energy with phase support |
| --- | --- | --- | --- | --- |
| 30 | continuous | 0.0695 | 15.9255 | 41.62% |
| 30 | random dwell phase | 3.6321 | 35.1535 | 50.62% |
| 40 | continuous | 0.00627 | 1.6477 | 89.27% |
| 40 | random dwell phase | 0.27925 | 24.7023 | 89.16% |

[30 continuous](../local-evidence/helper-two-tone/pilot-128-r8-30-continuous.log),
[30 random](../local-evidence/helper-two-tone/pilot-128-r8-30-random.log),
[40 continuous](../local-evidence/helper-two-tone/pilot-128-r8-40-continuous.log) and
[40 random](../local-evidence/helper-two-tone/pilot-128-r8-40-random.log) all peak
at zero corrected RF. There are zero new-bound prediction escapes over
18,633,600 dense prediction checks. At 30 dB-Hz, the old gate admits zero fits;
the new gate admits 301/596 continuous and 360/635 random-phase fits. Thus a
phase software gate, rather than a proven physical 30 dB-Hz limit, explains that
particular rejection. The controls exclude the payload notch and ordinary
128-second-symbol completion; remaining 3.63 dB random-phase loss is material.
The [initial missing-time setup failure](../local-evidence/helper-two-tone/pilot-128-r7-setup-missing-time.log)
and [first off-by-one diagnostic](../local-evidence/helper-two-tone/pilot-128-r7-30-continuous-r1-offbyone.log)
remain separately retained, not relabeled successful runs.

The [cached HF40 rerun](../local-evidence/helper-two-tone/hf40-r6.log) finishes
incomplete: one pending bit under a non-TX epoch and no END. It captures 453,561
samples/74.1113 audio seconds for 180.524 CPU/180.288 wall seconds. There are
84,547 FFT jobs versus 41,180 in R5; 2,557 cache builds/81,990 hits (96.98%) use
164,008 bytes per owner, with 41 FFT-executing owners using the serial-slot-1
path, not 41 observed concurrent workers. True TX epoch 40 owner 47 still
executes zero FFTs. Qualification is 98.269%, measured C/N0 median 23.416 dB-Hz,
U median 0.248592 Hz, with 220 certified blocks spanning 72.7544 seconds.
The [fixture binary pin](../local-evidence/helper-two-tone/hf40-r6-binary.sha256)
retains this run's source scope. Common hot-feed CPU is 62.24 versus 127.39
seconds descriptively; different fresh keyed waveforms and work prevent an exact
causal speedup ratio or passing HF acceptance claim.

The exact-lattice workspace idea saves only 38,880 bytes, leaving deficits
469,191/1,232,001 bytes for 83/89 pairs. It was rejected without a production
edit; the original constructor failures remain below. The first new
[full-composite N0CALL metadata component](../local-evidence/helper-two-tone/metadata-two-tone-r7.log)
decodes 7/7 references/frames. Earlier zero-frame failures remain below. The completed R8 metadata geometry and
CLI coverage is above; independent-LF Session work and Live optional metadata
routing remain incomplete.

### R6 focused integration and bounded actual-RF Session

The independently reviewed serial clock-row cache passes its
[direct scratch-linked production check](../local-evidence/helper-two-tone/hf-native-batch/production-test-r2.log):
47,462,282 exact score bit-pattern comparisons, seven cohorts/nine clocks/301
pairs, 63 builds, 2,044 hits and 164,008 charged bytes. Actual acquisition is
nonzero (2,107 jobs/23,731,451 trials), with five ordered candidates and two
ordered bursts matched. Original serial-slot-1/cache and parallel-slot-2/no-cache
paths remain. This is focused functional equivalence, not measured throughput.
The [original CTest fixture failure](../local-evidence/helper-two-tone/hf-native-batch/production-test.log)
(`changed key/source owner borrowed another private row`) remains failed. The
[dependency rebuild](../local-evidence/helper-two-tone/hf-native-batch/production-build-r2.log)
was separately interrupted (exit 130); the scratch-linked pass does not turn that
interruption into a build/CTest pass. The later
[integrated R6 build](../local-evidence/helper-two-tone/integrated-r6-build.log)
and [three focused CTests](../local-evidence/helper-two-tone/focused-r6.log)
pass: clock rows 29.84 seconds, pilot stage 1.07 seconds and tracker 5.91 seconds
(36.82 seconds total). These remain focused checks, with full groups pending.

The [whole-aperture stage check](../local-evidence/helper-two-tone/pilot-stage-whole-r6.log)
scores all 145,472 helper-present samples, including startup: phase-corrected
loss is 1.227/1.410/1.266 dB for 40 dB-Hz fixed, 60 dB-Hz fixed and 60 dB-Hz
actual-RF laboratory controls, versus frequency-only 17.63/18.40/6.82 dB. Corrected
RF peaks are at zero; 80.4% of energy has phase support, with no covered-phase
escapes. Earlier supported-sample RMS concealed this real 1.2–1.4 dB whole-aperture
loss. This uses an isolated linear Robust contribution through the Hilbert
translator; the payload notch, complete symbols and 128-second reception remain
untested. It does not replace the earlier frequency-interval failures.

The bounded [actual-RF Session check](../local-evidence/helper-two-tone/actual-rf-session-r6.log)
passes with [complete trace](../local-evidence/helper-two-tone/actual-rf-session-r6.jsonl)
and no dropped records. Exact command:

```sh
taskset -c 2-11 build/dev/test_helper_actual_rf_session local-evidence/helper-two-tone/actual-rf-session-r6.jsonl > local-evidence/helper-two-tone/actual-rf-session-r6.log 2>&1
```

It exercises 73 physical-RF model segments at 7/7.7/8.4/9.1 MHz, 400 ms dwells,
independent phases (seed 49273) and 12 ms retune settling. All 887 qualified
reference measurements contain truth, with maximum error 0.00043077 Hz and
69 phase updates. The ordinary-work counter reaches 210 actual jobs, not an
observed concurrency count; both frequency and phase assistance flags are true. Three pending commits retain one identity and
exact `001`, followed by one fully scored physical END. It processes 232,327
logical samples, peaks at 38,526,009 bytes, and bounds raw staging to 12,800
samples at 32 kHz (400 ms). Process CPU is 1.603 seconds/1.101 wall seconds with
permitted support cores 2–11; worker concurrency and throughput are not qualified.

This is an RF-only, exact-audio-clock laboratory control with DATA simulation
SNR 40 dB, a very strong pilot and zero drift. Encrypted Auto/rolling reception
uses no GPS clock, known TX timestamp or decoded timecode. It is not supplied
HF acceptance, independent-clock, 128-second-symbol or hardware-adapter coverage.
This original narrow proof remains bound to its tested source; the subsequent
source-band and covered-feed checks are recorded above.

The [R6 binary record](../local-evidence/helper-two-tone/r6-build-binaries.sha256)
pins GUI `ecac66bb25f3e681474a170ae56149b34340f22df0597ed96aa5e4a688cdea80`
and archive `96977be799b054e0a33611cc98aa823fcd3afa26961d0a6319c3d694cb5ad8d8`.
These rebuilt manual-test binaries do not certify pending source changes or a
release.

### Preceding R5 focused integration

The [22-target R5 build](../local-evidence/helper-two-tone/integrated-r5-build.log)
passes, followed by [four focused checks](../local-evidence/helper-two-tone/focused-r5.log)
(geometry, pilot phase, pilot stage and Planner) in 7.06 seconds. Whole-source
inputs (949 pins) and produced binaries are bound by
[R5 inputs](../local-evidence/helper-two-tone/r5-inputs.json) and
[binary hashes](../local-evidence/helper-two-tone/r5-binaries.sha256); these are
source/binary bindings, not release certification. Component provenance remains
in [gap-final hashes](../local-evidence/helper-two-tone/gap-final-hashes.txt).
That R5 measured tracker workspace is 403,016 bytes, separately bound from the
current continuous-estimator work.

The fixed-context numeric frequency fallback is implemented. It retains the
ten current complex observations, permits a single independent phase gap in
that bounded context, and gives no phase authority to a gap fit. The
[current component baseline](../local-evidence/helper-two-tone/segmented-gap-final.txt)
has 1,551 published estimates with no observed interval escapes. The
[12-seed blind 30 dB-Hz ensemble](../local-evidence/helper-two-tone/segmented-gap-ensemble-final.txt)
retains one escaped straight-fit estimate among 2,225: seed 2, support date
9.370646 seconds, error −0.222884 Hz versus U 0.215283 Hz. It is a failed coverage
control despite a successful final estimate. The
[actual-waveform matrix](../local-evidence/helper-two-tone/segmented-gap-actual-final.txt)
has twelve cases, 2,071 frequency estimates and 1,339 sampled phase fits with no
observed escapes. At sampled pilot C/N0 24 dB-Hz, composite static/random-hop
availability is 97.45%/96.39%, with final U about 0.222/0.250 Hz. This improves
software availability without claiming those radii make every ordinary profile
affordable. [Twelve noise-only seeds](../local-evidence/helper-two-tone/segmented-gap-noise-final.txt)
never qualify. Tracker CPU/audio is about 0.00084–0.00226 across these component
controls, excluding the ordinary receiver, staging/frontends, generation and GUI.

The [R5 phase-stage output](../local-evidence/helper-two-tone/focused-r5-output.log)
uses causal broad discovery without a truth-frequency seed. Constant 40/60 dB-Hz
payload relative RMS is 0.0370012/0.00652916 versus frequency-only
1.54784/1.4387; the 60 dB-Hz case retains ten frequency-interval escapes.
A laboratory actual-RF control uses independent phases at 7/7.7/8.4/9.1 MHz:
328 dated consumed-geometry checks have zero observed escapes, payload relative
RMS 0.0059667 versus frequency-only 0.999442. Its USB, zero audio-clock error
and known retune dates are explicit laboratory inputs. There is no production
rig-retune adapter or unresolved-clock qualification. Storage remains capped at
19,200 raw samples/76,800 bytes at 48 kHz, separately from the tracker.

The prior GUI SegFault is narrowed to partial-bank retirement with null
`receiver_storage`; the minimal guard is implemented and independently reviewed.
The [original crashing key-control subsection](../local-evidence/helper-two-tone/tone-key-controls-r5.log)
passes. The [full controller/Planner/self-check repair run](../local-evidence/helper-two-tone/gui-repair-r5.log)
passes 3/3: controller 142.38 seconds, Planner 74.32 seconds and self-check
9.95 seconds (142.40 seconds total elapsed). This validates the narrow null guard
and selected GUI paths; final full groups remain pending. The original 38/39
failed GUI group remains below.

The [HF40 R5 rerun](../local-evidence/helper-two-tone/hf40-r5.log) finishes
failed/incomplete (exit 2), again zero of eight bits. It uses the same exact
flags as the prior command below, changing only the evidence directory and log
from `hf40` to `hf40-r5`. It captures 52.977 audio seconds for 180.455 process
CPU seconds. Reference qualification is now 97.58%, and true TX epoch
`1800000040` is admitted at 34.110 seconds, but that owner executes zero FFTs.
The ordinary roughly 301-cell work cost starves its processed frontier. Thus the
fallback improves reference availability but does not complete HF40 reception;
this is a receiver-work limitation, not a demonstrated physical precision limit.
The earlier low-availability HF40 failure remains separately retained below.

All three helper-OFF GPSDO-configured simulation controls pass exact eight bits,
one visible row, prefixes, pending identity and ordinary scored END:
[HF40](../local-evidence/helper-two-tone/hf40-gpsdo-control.log),
[HF crystal](../local-evidence/helper-two-tone/hf-crystal-gpsdo-control.log) and
[Rate-360 Fake FHSS](../local-evidence/helper-two-tone/hf-fhss-gpsdo-control.log).
They are modeled GPSDO controls, not physical GPSDO-hardware tests. They do not
make a failed Helper run pass or establish a matched end-to-end speedup.

The exact EME wrapper command was:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --eme --helper on --helper-power-w 15000 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 600 --ephemeral-key --evidence-dir local-evidence/helper-two-tone/eme15k-r5 > local-evidence/helper-two-tone/eme15k-r5.log 2>&1
```

The supplied [15 kW EME R5 run](../local-evidence/helper-two-tone/eme15k-r5.log)
now executes the 5.8 GHz GPSDO-OCXO encrypted Auto/rolling 128-second-symbol
configuration, Default/70 ms, fixed 2 GiB workspace. It **fails** (exit 1):
1,827.731 captured audio seconds, 80.983 process CPU seconds/78.483 wall seconds,
zero admitted owners and zero bits. There are 53,012 provisional compact CPU
admission rejections, with candidate pairs 83–389 (median 87). Minimum predicted
serial CPU/audio is 1.221164, above the configured 0.6 objective. That is a
software model/admission gate, not measured receiver throughput or a physical
link limit. No receiver owner executed to establish PHY failure or full-symbol
performance.

EME raw measured pilot C/N0 median is 30.0399 dB-Hz, U median 0.084627 Hz and
qualification 99.9369%; all 5,397 corrected blocks have valid certificates. These
measurements establish sustained assistance availability, not useful ordinary
reception while every candidate is rejected. Configured RF-budget C/N0, sampled
channel noise and measured pilot C/N0 retain their distinct meanings. Native
cache work has the focused source/equivalence acceptance and R6 build/selected
CTest passes above; full groups and any measured throughput benefit remain pending. Final full regression groups, complete payload/cable checks
and intended-hardware throughput qualification remain pending.

The [EME GPSDO-configured control](../local-evidence/helper-two-tone/eme-gpsdo-control.log)
finishes **incomplete** (exit 2), with zero bits and 31 owners. Exact command:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --eme --gpsdo-only --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 600 --ephemeral-key --evidence-dir local-evidence/helper-two-tone/eme-gpsdo-control > local-evidence/helper-two-tone/eme-gpsdo-control.log 2>&1
```

It captures 16,384 samples at 6,000 Hz (2.73067 audio seconds), using
574.549985 process CPU seconds/600.967447 wall seconds and peaking at
1,453,811,343 bytes. Only seven feed calls covering input 0…14336 complete;
the next is interrupted. Captured input is not a processed frontier. No full
128-second symbol or physical END completes, and this partial control establishes
no matched CPU gain or physical sensitivity limit.

The separate [actual-constructor discriminator](../local-evidence/helper-two-tone/eme-cost-r5/RESULT.md)
uses a copied R6-cache archive
`23128c7b31f9b31da6c48965a5c6fa815961b3aeab9980a880b4692e2d1fafae`,
distinct from the R5 runtime above. Its fixed 2 GiB configuration grants a
10,092,055-byte strategy share:

| Actual pairs | Complete hypotheses | Required bytes | Shortfall |
| --- | --- | --- | --- |
| 83 | 1,286 | 10,600,126 | 508,071 |
| 89 | 1,379 | 11,362,936 | 1,270,881 |

Both actual constructors return typed `PatternCompactAdmissionPending` before
any PCM. [83-pair](../local-evidence/helper-two-tone/eme-cost-r5/execution-83.json)
and [89-pair](../local-evidence/helper-two-tone/eme-cost-r5/execution-89.json)
receipts retain diagnostic exit 3, commands, hashes and reaped processes. The
Live provisional CPU gate therefore masks an additional actual workspace gate;
no lane, timing/clock alternative or grant was changed. No receiver execution,
throughput, payload/END or bank-count gain is measured by this discriminator.
The later cached HF40 result remains incomplete as recorded above.

### Preceding R3 focused integration and supplied cases

The [R3 GUI rebuild](../local-evidence/helper-two-tone/gui-build-r3.log) and
[GUI self-check](../local-evidence/helper-two-tone/gui-self-check-r3.log) pass;
the self-check is one case, 8.89 seconds. The original
[R3 focused group](../local-evidence/helper-two-tone/focused-r3.log) is **11/14**,
with failed `helper_pilot_stage`, `helper_tone_tracker` and `helper_sim` cases.
These remain individually failed. Narrow repairs then pass:
[simulation](../local-evidence/helper-two-tone/helper-sim-r3-repair.log) measures
suppression in the actual DATA band with unchanged thresholds;
[tracker migration](../local-evidence/helper-two-tone/tone-tracker-migration-output.log)
uses the current two-tone pilot route; and the
[phase-stage fixture](../local-evidence/helper-two-tone/pilot-stage-r3-corrected.log)
accepts first qualification at exactly one second, correcting its strict-boundary
off-by-one assertion. These fixture repairs do not constitute a broad regression
pass. The completed [GUI group](../local-evidence/helper-two-tone/gui-group-r4.log)
is **38/39**, with `gui_controller` SegFault at 74.13 seconds and total elapsed
155.99 seconds. It is a failed group, not full GUI qualification.

The migrated actual two-tone tracker control spans 113.671 seconds, with 100%
frequency availability, median/p90 U 0.00463568/0.00680744 Hz, maximum frequency
error 0.000470372 Hz and zero observed interval escapes. Comparing actual pilot
with auxiliary/Robust enabled versus disabled gives maximum bias 0.0000540165 Hz.
The actual selector retains 45 of 369 pairs and all nine unresolved clock
alternatives. This is a bounded mask/precision check, not an end-to-end CPU gain.
That preceding 48 kHz tracker workspace is 382,392 bytes.

The causal phase stage uses broad discovery without a truth-frequency seed.
At 40/60 dB-Hz, first qualification is exactly 1.0 second and payload relative RMS
is 0.0374703/0.00640305, versus frequency-only 1.51582/1.43624. It uses a bounded
19,200-sample/76,800-byte raw stage at 48 kHz, and measured component CPU/audio
is 0.04444/0.04390. No wrong-tone selection occurs, but the 60 dB-Hz control has
**ten frequency-interval escapes**, versus zero at 40 dB-Hz. RF mapping is
explicitly untested in this constant-scale laboratory control. Its phase
improvement does not calibrate pooled-frequency intervals or prove ordinary
payload reception.

The [preceding plain-CW 30 dB-Hz trace](../local-evidence/helper-two-tone/promoted-plain30-trace.txt)
(seed 1, production gate 1) has **19 escaped published estimates among 185**,
repeated across 380 polls. Maximum observed `abs(error)/U` is about 1.2005;
final error +0.051889 Hz lies inside final U 0.085746 Hz. Final containment does
not repair earlier escapes. The [preceding component matrix](../local-evidence/helper-two-tone/segmented-promoted.txt)
therefore fails coverage (exit 2). The
[preceding actual-waveform matrix](../local-evidence/helper-two-tone/segmented-promoted-actual.txt)
has four final-availability failures at 24 dB-Hz; its 30/40 dB-Hz cases finish
qualified with no observed frequency escapes. Earlier failed component outputs
below retain their own implementation versions. All published intervals remain
conditional engineering estimates, not calibrated confidence guarantees.

The optional full-composite auxiliary decoder still fails: both
[initial](../local-evidence/helper-two-tone/aux-metadata-two-tone-first.log) and
[strong](../local-evidence/helper-two-tone/aux-metadata-strong-first.log) attempts
find 48 references and zero metadata frames. The
[matched discriminator](../local-evidence/helper-two-tone/aux-metadata-discriminator.log)
decodes six frames on isolated raw auxiliary, zero after its notch and zero on
the notched full composite. This isolates a route limitation; metadata is not
required to qualify pilot frequency assistance. The accepted capture-band guard
accounts for both tones without changing already-generated supplied-case samples;
its focused `helper_sim` check passes.

The actual supplied-run commands are retained exactly, with two allowed CPUs
and fixed 2 GiB workspace:

```sh
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf --helper on --helper-power-w 40 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 180 --ephemeral-key --evidence-dir local-evidence/helper-two-tone/hf40 > local-evidence/helper-two-tone/hf40.log 2>&1
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf-crystal --helper on --helper-power-w 15000 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 0ms --bit-count 8 --wall-seconds 180 --ephemeral-key --evidence-dir local-evidence/helper-two-tone/hf-crystal15k > local-evidence/helper-two-tone/hf-crystal15k.log 2>&1
taskset -c 0,1 build/dev/test_helper_normal_acquisition --hf-fhss --helper on --helper-power-w 1500 --workspace-bytes 2147483648 --runtime-metrics --work-trace --clock-sync default --audio-error 70ms --bit-count 8 --wall-seconds 180 --ephemeral-key --evidence-dir local-evidence/helper-two-tone/hf-fhss1500 > local-evidence/helper-two-tone/hf-fhss1500.log 2>&1
```

The preceding [HF40 timing-wrapper setup attempt](../local-evidence/helper-two-tone/hf40-time-setup-failure.log)
failed before the test: `taskset: failed to execute /usr/bin/time: No such file or directory`.
The actual [HF40 run](../local-evidence/helper-two-tone/hf40.log) is failed/incomplete
(exit 2): zero of eight bits, 113.418 captured media seconds, 180.106 wall seconds,
180.694 process CPU seconds and 42,317 native FFT jobs, with zero input health
faults/drops. Raw reference qualification covers about 2.79% of observations;
corrected certificates cover 0.512 of 108.548 scored seconds. True TX epoch
`1800000040` is never admitted. This identifies a software availability/admission
failure, not a physical precision limit or successful weak-link qualification.

The [strong HF15k run](../local-evidence/helper-two-tone/hf-crystal15k.log) and
[Rate-360 Fake-FHSS run](../local-evidence/helper-two-tone/hf-fhss1500.log) return
`decoded_exact` (exit 0) for their primary fixture checks, preserving all eight
wire/source bits, primary prefixes, pending identity and ordinary scored END.
HF15k exposes six rows/five secondary rows. Four secondary rows fail the expected
source-start-prefix comparison. Independent
[source-identity review](../local-evidence/helper-two-tone/row-identity-review/discriminator.log)
uses [actual helper discriminators](../local-evidence/helper-two-tone/row-identity-review/discriminator.cpp),
with no production edit. Signal 4's false/truncated wire `01` is not a canonical
`10001100` suffix: its first two interior Data addresses agree, then diverge and
end early. Its separate row birth is due to an actual gap of `T − 55` samples,
outside the 34-sample association tolerance. This remains a strict qualification
failure; no merge or threshold weakening is proposed.

Signal 6 is different: candidate 11→13/source-coordinate changes preserve the
published prefix `11`, while old Data epochs 75,89,104 become 75,89,103. The new
three-symbol sequence matches the canonical suffix; the old one does not.
Independent review finds this a permitted compatible-prefix correction adding
the correct third bit, not an identity defect by itself. The review does not
make signal 4, all GUI rows or strict cable qualification pass.
Rate-360 has one visible primary row, epoch `1800000005` versus actual TX
`1800000006`; true-epoch owner 13 also contains the full exact wire. The precise
alias/source-presentation interpretation remains unqualified.

Executed work is now audited for these helper-ON cases:

| Supplied case | Captured audio (s) | Process CPU (s) | CPU/audio | Native FFT jobs | Input transforms | Peak DSP bytes |
| --- | --- | --- | --- | --- | --- | --- |
| HF15k | 194.106209 | 44.332381 | 0.228392 | 4,627 | 304 | 656,919,635 |
| Rate-360 Fake FHSS | 24.666736 | 36.119178 | 1.464287 | 29,106 | 539 | 551,392,408 |

HF15k records 206 owners, 161 executing native FFT jobs. Its primary owner narrows 127→8→…→2 before its first
FFT and executes 56 FFT jobs under the 127→2 mask. Other measured work includes
3,290 FFT jobs with 43→2 masks, 525 with 43→3 and 448 with 127→2. This is actual
existing-owner narrowing, not a gain inferred from interval ratios. There is no
matched helper-OFF CPU control. First qualification is at 0.968 seconds with
99.51% availability; U median/minimum/maximum is 0.01000/0.00911/0.06513 Hz.
There are 966 actual phase updates and 582/583 corrected blocks.

Rate-360 records 40 owners, 39 executing native FFT jobs. Its actual masks remain 9→9: factory
2,075-pair geometry is constructor metadata, not executed broad search removed
by Helper. The folded-mono route supplies frequency-origin centering without
phase correction. Qualification availability is 95.63%, median U 0.00747 Hz and
measured pilot C/N0 56.501 dB-Hz. Worker concurrency is untraced in both cases;
zero reported simulation backlog is not measured native-device backlog. Input
health counters are zero. These costs compare different profiles and clock
policies and establish no matched GPSDO/helper speedup.

HF15k applies actual +3 kHz/+100 ppm impairment but launches with
0 ms audio-error allowance; HF40 and FHSS use Default/70 ms. Rate-360 uses actual
+30 Hz/+100 ppm and fixed physical RF. They do not establish hardware-hop phase
following, matched GPSDO speedup or equal-clock-policy comparisons. The
[current EME metadata](../local-evidence/helper-two-tone/eme-metadata-current.log)
resolves 15 kW, 5.8 GHz GPSDO-OCXO, encrypted Auto/rolling 128-second symbols and
Default/70 ms, but executes no waveform, receiver bank or throughput check.

These supplied-run results precede R5 and retain their tested-source bindings;
the newer R5 component/focused results above do not retroactively qualify them.
The original scratch context experiments and every failed result remain retained.
The source-identity review is complete with no production change: signal 4 stays
failed, while signal 6's compatible-prefix correction is permitted.

### Earlier two-tone component baseline — superseded

The following outputs precede the current gate/context integration above. Original
failures remain retained and do not qualify newer source, even when a later case
passes.

The segmented path first discovers the strongest tone across the actual
oscillator/capture prior, without auxiliary identification or a ±100 Hz cap.
At 48 kHz it uses an 8,192-sample seed FFT, ten 20 ms Hann projections per
200 ms context, and at most 192 dated frequency-only records over 36 seconds.
The [synthetic final component matrix](../local-evidence/helper-two-tone/segmented-final.txt)
reports 382,168 bytes and about 0.00082 tracker CPU seconds/audio second for
qualified cases; this excludes generation, frontend, Robust and GUI work and is
not an end-to-end throughput measurement. Constant/supported drift, wide
+3,000.37 Hz offset, synthetic mixtures, random per-hop phase and final loss are
covered narrowly. The 40 dB-Hz loss case withdraws by 28.4 seconds after loss at
28 seconds. At 20 dB-Hz it does not qualify; this is an implementation gate,
not proof of a physical SNR threshold.

The [12-seed synthetic 30 dB-Hz ensemble](../local-evidence/helper-two-tone/segmented-ensemble-final.txt)
**fails interval coverage** (exit 2): seed 2 has one escaped dated estimate,
repeated over 20 qualified polls. There are no wrong-tone selections in these
controls. The [matched seed-2 dated-boundary control](../local-evidence/helper-two-tone/segmented-seed2-final.txt)
removes the observed escape but reduces poll availability from 94.92% to 48.53%;
it does not erase the original blind-hop failure. These short seeded results do
not establish calibrated confidence.

The [actual Generator plus actual Robust 12-case matrix](../local-evidence/helper-two-tone/segmented-actual-final.txt)
uses pilot C/N0 30/40/60 dB-Hz, auxiliary/Robust enabled versus disabled, a
137 ms source alignment, +3,000.37 Hz offset and independently randomized hop
phases. Its sampled **pilot** C/N0 is the noise input; total straight Helper C/N0
is 0.4576 dB greater. It is not configured RF-budget or measured cable C/N0.
Two static 30 dB-Hz cases finish unqualified, so the matrix is failed (exit 1),
with zero escaped frequency polls and wrong-tone selections. Ordinary reception
is explicitly unexecuted. Representative matched results after 38 seconds are:

| Pilot C/N0 | Hop phases | Auxiliary/Robust | Last frequency error (Hz) | Last interval radius U (Hz) | Poll availability |
| --- | --- | --- | --- | --- | --- |
| 30 | independent | disabled | +0.03598 | 0.19155 | 94.34% |
| 30 | independent | enabled | +0.02515 | 0.19308 | 94.34% |
| 40 | static | disabled | −0.00700 | 0.02570 | 97.45% |
| 40 | static | enabled | −0.00695 | 0.02574 | 97.45% |
| 40 | independent | disabled | +0.01864 | 0.04328 | 95.87% |
| 40 | independent | enabled | +0.01092 | 0.04374 | 95.87% |
| 60 | static | disabled | −0.000774 | 0.004949 | 97.45% |
| 60 | static | enabled | −0.000719 | 0.005230 | 97.45% |

The 60 dB-Hz mixture's measured pilot C/N0 is about 52.64 dB-Hz; sampled noise
input is not achieved measurement quality. The mixtures expose actual
self-interference rather than assuming an external-noise-only floor. These
component timings were contended and provide no ordinary receiver CPU gain.
Last qualified estimates can be older than the final captured frontier; the
30 dB-Hz static final observation age is 0.529 seconds, consistent with its
final qualification loss. Support spans near 36 seconds while the individual
coherent aperture remains 0.2 seconds.

The [local context-phase matrix](../local-evidence/helper-two-tone/segmented-context-phase-actual.txt)
reports no phase-fit escapes over its tested local supports, with maximum
observed phase error about 0.056 cycles for the mixed 40 dB-Hz independent-hop
case. It retains the same two final frequency-availability failures. This is
local component evidence only. Raw projection phase is diagnostic; pooled
oscillator frequency has no phase authority. Source now contains a separately
funded raw FIFO capped at 400 ms and local phase/slope APIs. Their integrated
payload preservation and ordinary correction benefit remain under validation.
Dated signed-RF scale/sideband/fade controls use laboratory-known zero clock
error. The application has no actual rig-retune adapter; fixed-physical-RF Fake
FHSS does not establish hardware-hop phase following.

A non-integrated [320 ms alternative](../local-evidence/helper-two-tone/segmented-32-final.txt)
improves static 30 dB-Hz final U from 0.0870 to 0.0539 Hz, but blind random-hop U
worsens from 0.1730 to 0.3089 Hz and availability from 95.34% to 79.97%. A
[known-boundary 320 ms control](../local-evidence/helper-two-tone/segmented-32-dated-final.txt)
reaches U 0.0599 Hz at 30 dB-Hz with only 20.08% poll availability. This is a
window-alignment tradeoff, not a physical limit or an integrated improvement.
Earlier endpoint/setup outputs remain individually retained in the same evidence
directory; their filenames do not qualify newer sources.

### Current model references and remaining acceptance

Planner's noise-only reference uses pilot power after the split. The
[uninterrupted-tone reference](helper-carrier.md#link-planner-and-lead-time)
is `3 sqrt(3 / (2 pi² (C/N0) T³))`, supported by the linked JPL/NASA reference.
Independent unknown phases instead accumulate information as
`sum_i((C/N0)_i T_i³)`. At 30 dB-Hz, 25 ideal 400 ms dwells give about ±0.029 Hz;
uninterrupted ten seconds gives ±0.00117 Hz. Practical prediction includes
separate context/phase-noise/model costs and does not replace measured coverage.
Baseline lead is separate from the maximum 36-second precision extension;
transmission begins at that cap without automatic qualification. The ±0.1 Hz
milestone does not establish 128-second-symbol feasibility. Conditional
receiver/workspace pricing uses the existing receiver-cost model with unresolved
clock/timing work, not interval-width ratios, and excludes whole-process costs.

Full current-source preservation-contract, GUI/native/sanitizer and complete
supplied HF/5.8 GHz EME acceptance remain pending. Actual receiver work, existing
and newly admitted candidate narrowing, workers, process CPU/audio, startup,
sustained backlog, drops/resets, complete payloads, next-poll progress, row
identity and fully scored physical END need integrated evidence. The completed modeled
GPSDO controls above retain their scope; causal CPU comparisons and final exact
launch/source bindings remain required, with fixed workspace bytes.
No complete long-symbol EME, strict cable, physical GPSDO-substitution or
release-certification pass is claimed. Historical prefix/suffix failures below
remain failed even when their final canonical row was correct.

## Superseded R16 single-tone Helper Carrier candidate

All results in this section and its subsections retain their own source/binary
bindings. They do not qualify the changed two-tone waveform or current overlay.

R16 remains an experimental manual tracker/NCO route feeding one ordinary
receiver bank. New acquisition requires fresh helper qualification and a
suitable typed constructor outcome; bounded already observed first-symbol
continuation and accepted-owner progress/physical-END rules remain ordinary.
There is no parallel broad bank or historical-PCM search. Phase correction is
inactive and Live metadata decoding unqualified; folded real mono has only
frequency-origin centering, hardware FHSS following is unsupported and Planner
CPU estimates are not overall runtime calibration.

The [current R16 coherent receipt](../local-evidence/helper-v16/integration-build-r16/execution-result.json)
(`c47163f8`) records application/focused-build/runtime passes in
107.363/2.227/43.144 seconds, including four focused checks, with unchanged
inputs and reaped processes. [Bindings](../local-evidence/helper-v16/integration-build-r16/production-binaries.json)
(`6bff6fc3`) pin GUI `571664fa0faa8bb849165698b57ff6327717c37cd9ba180b2eb295289350f1c5`
and core `3148dd7b`. The reviewed five-line same-state TRUE guard skips duplicate
dispatch-control preparation only when both permissions are already true;
frequency setters and numeric pushes still prepare current closure. The matched
eight-bit shared-pulse fixture retained exact per-push prefixes, identity,
pending/END, changing frequency windows, work and storage versus repeated TRUE
updates. No speed claim follows. The R13 core `05bdf896`/GUI `c7d7100c` baseline
remains separately preserved. The manual-helper Auto policy waits at a supported
long-symbol shared-pulse NEW candidate's boundary when a finite provisional
serial-work price exceeds the receiver CPU objective. Existing/started owners,
helper OFF, forced strategies, unsupported/unknown estimates keep their prior
behavior; the estimate interface opts in explicitly, without an Hz threshold.
It is not an overall calibrated CPU cap or a measured EME cure.

Closeout retains the integrated one-bank/NCO, measured RF/LF narrowing, ordered
lookup-cache and CPU-admission paths with unchanged ordinary payload contracts.
Two-tone reception is future work: a separate pilot must remain continuously
unmodulated. The [isolated pilot reference](../tools/pilot-reference/README.md)
is not used by the application. Its [sealed component receipt](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/pilot-closeout-r1/checks/execution-result.json)
(`b3ae62c6`) records compile and 11-case passes in 2.787 seconds, with unchanged
inputs and empty owned process groups. The exact-copy source pins are
`77dc2c16` (reference), `c1c2282a` (coarse FFT), `284a665f` (test) and `0514a32d`
(README); the README supplies the standalone reproduction command.

In the single-seed 24 kHz constant-carrier controls with Wiener sigma 0.01:

| C/N0 (dB-Hz) | Median interval radius U (Hz) | Final U (Hz) | Final carrier-trend error (Hz) |
| --- | --- | --- | --- |
| 20 | 0.012551 | 0.008665 | -0.0003885 |
| 30 | 0.009660 | 0.007997 | +0.0000214 |
| 40 | 0.009277 | 0.007924 | +0.0001577 |

The matrix also passed ±0.03 Hz/s trends, wide +3000.58 Hz capture, a large sample
origin and pilot loss. Present cases first qualified at a captured frontier of
4.330667 seconds; loss at 26 seconds withdrew qualification by 26.730667 seconds.
No qualified interval/domain/date escapes occurred in these controls. This is
conditional engineering evidence, not statistical coverage: intervals assume
the stated local-linear trend and Wiener model, include an empirical 0.005 Hz
allowance, and describe actual centers about 0.2 seconds old with 0.4-second
updates. They provide no future lease, separate RF/audio-clock estimate or epoch.

At 24 kHz the component used 534,056 bytes, including its current FFT buffer;
its history is bounded numeric observations, not historical PCM search. Thirteen
FFTs processed each 38-second control. Component CPU/audio was about
0.00083–0.00086, including diagnostic callbacks but excluding signal generation.
Application/two-tone integration, ordinary receiver CPU savings, physical cable
operation, payloads and full qualification were not executed by this component.
The application cable/EME failures below remain. Full qualification is deferred,
not passed.

The [final handoff check](../local-evidence/helper-v16/closeout-final/execution-result.json)
(`b051eacb`) rebuilt the unchanged GUI in 1.968 seconds, compiled the preserved
`tools/pilot-reference` sources in 2.520 seconds, and passed their eleven-case
reproduction in 0.968 seconds. The pilot binary exactly matched the reviewed
component binary (`0c993f90`); GUI/core hashes stayed `571664fa`/`3148dd7b`.
Input guards stayed unchanged and all owned processes were reaped. The four
R16 application checks above are reused; no broader qualification ran here.

The [metadata-depth R1/R2 packet](../local-evidence/helper-v16/metadata-tone-depth-r1/README.md)
was stopped and remains unintegrated; [setup failure `9053df85`](../local-evidence/helper-v16/metadata-tone-depth-r1/checks-r1/setup-failure.txt)
occurred before any compiler child. The [exact-seed cache component](../local-evidence/helper-v16/projection-cache-seed-r1/component-execution-result.json)
passed separately but is not integrated: actual normal-correlator CPU evidence
and cache-hit/arena issues remain unresolved. Neither isolated experiment is a
current feature. The conditional WLS/L1 numerical rejection below is not a
physical precision floor.

The [R12 focus](../local-evidence/helper-v16/integration-build-r12/execution-result.json)
was 11/13 despite a passing application build: a fixture missing production
Fake-FCC geometry was repaired for R13. The original UTC assertion also failed
on both pre-CPU R11 overrides and R12 in the [matched counterfactual](../local-evidence/helper-v16/helper-cpu-admission-r1/counterfactual-r11/execution-r1/result.json)
(`a4a42434`), so it is not attributed to the new gate. The subsequent full
[R14 simulation check](../local-evidence/helper-v16/integration-build-r14/execution-result.json)
failed at a later stale fixed-epoch/known-simulation conjunction; both failures
remain retained. The reviewed test-only repair kept the zero rolling-epoch
assertions, added zero sampled-refresh frontend work, and required the exact
five RF lanes in the expanded helper domain; the original independent three
were already separate. The [R15 receipt](../local-evidence/helper-v16/integration-build-r15/execution-result.json)
(`b78ac254`) records application/test-build/full `simulation_estimate` passes in
1.817/9.372/28.579 seconds with unchanged runtime GUI/core, source guards and
reaped processes. Thirteen R13 focus passes are reused, not rerun. This is not a
full contract/native/sanitizer or runtime-throughput result. These R13/R15
checks precede the R16 guard, and all full-case HF, attachment, cable and EME
measurements below remain bound to R11. The rejected
merge-bypass proposal was not integrated because it reopened known prefix-rewrite
behavior.
The [grid-union component](../local-evidence/helper-v16/helper-grid-union-r1/execution-r1/geometry.log)
reduced EME 69→35 pairs while preserving the full declared clock domain; ordinary
defaults stay unchanged. This is component coverage, not an actual EME pass.

The [R11 controlled HF result](../local-evidence/helper-v16/normal-acceptance-private/r11-final-controlled/r16-r11-hf15k-3k-on-v1/sealed-HF-summary.json)
(`cbefbeb3`) passed all eight physical wire/source bits, retained prefixes,
pending identity, complete TX/channel drain and ordinary END with zero health
faults. Truth was actual +3,000 Hz CFO/+100 ppm audio clock on helper and DATA
before decimation. Whole-process CPU was 51.775831 seconds/195.756209 media
seconds (0.264491). Independently joined complete disjoint body intervals
covered 116.885131 media seconds/20.661108 ordinary-feed CPU seconds (0.176764);
this excludes interinterval gaps and is not whole-body process CPU. Raw helper
C/N0 median was 48.9042 dB-Hz, separate from RF-budget 55.7609 and sampled
reference 49.7403 dB-Hz. This is one controlled case, without strict matched
OFF/GPSDO speedup or physical hardware backlog claims.

The separate [R11 GPSDO-model helper-OFF simulation](../local-evidence/helper-v16/normal-acceptance-private/r11-final-controlled/r16-r11-hf15k-gpsdo-off-v1/sealed-GPSDO-summary.json)
(`344c3e41`) passed exact eight-bit source/wire, prefixes, identity, END and drain:
32.531333 CPU seconds/162.758170 media seconds (0.199875). It used raw 6,120 Hz
versus helper-ON 48,960 Hz, with different TX dates and helper lead, so it is not
a strict causal speedup or physical GPSDO comparison. These source/prefix/END
passes do not verify next-poll latency: their retained HF/GPSDO commit→poll
estimates are 46.919/30.301 seconds. Required progress timing remains separate
acceptance coverage.

The [R11 attachment result](../local-evidence/helper-v16/attachment-preservation-r11/execution-result.json)
(`0e8f3570`) passed matched OFF/ON all 2,432 wire bits, authenticated 17-byte
source, exact prefixes, same pending ID and physical END in 157.36 seconds.
Bounded trace coalescing kept this case within the unchanged cap. The preceding
[R9 trace-capped attachment failure](../local-evidence/helper-v16/payload-preservation-r9/execution-result.json)
remains failed/incomplete, and this small case does not qualify full-size
attachments or 1,024-byte text. Historical 1,024-bit binary passes are a different
scope. Current larger text has no passing qualification; earlier deadline
failures remain failed.

Both connected-cable R11 controls failed strict checks:
[Default](../local-evidence/helper-v16/live-reliability/cable-r11-host/default/execution-result.json)
(`8079dc58`) and [GPSDO-configured](../local-evidence/helper-v16/live-reliability/cable-r11-host/gpsdo/execution-result.json)
(`ce1c1e34`) eventually completed exact eight bits on ID 3, but an earlier
seven-bit suffix on ID 2 and aliases were unexpected. Correct final data does
not erase those failures. Health counters were zero, while whole-GUI CPU/audio
was 0.428860/0.384945 and mean measured helper C/N0 53.908/55.413 dB-Hz versus
configured 30.0206. Physical cable capture did not inject that RF loss/noise;
both controls had helper ON and different measured C/N0. The descriptive
whole-process costs are not a causal hardware-GPSDO speedup or weak-link/
GPSDO-substitution acceptance.

The [sealed R11 ordinary Auto EME result](../local-evidence/helper-v16/normal-acceptance-private/r11-final-controlled/r16-r11-eme15k-128s-on-v1/sealed-EME-summary.json)
(`436e9cbd`) is throughput-incomplete: 300.612 wall/301.331672 CPU seconds,
95.44 captured media seconds (3.1573 CPU/media), no bits/END and unfinished
TX/channel drain. TX emitted 416,732/1,173,277 samples, below valid payload end
960,000; this is not a PHY, truncation or END-failure proof. Body measurement
joins 208 finite disjoint intervals covering 68.8075 media seconds with
271.481088 ordinary-feed CPU seconds (3.94552 CPU/media); generation CPU was
1.896628. First admission had 79 pairs/`U` 0.0751499 Hz, with 94 admissions;
actual masks updated 79→73→63, so a stale-mask explanation is not established.
Recorded projected-carrier samples were 1.41428 billion with zero native jobs;
these units are not combined. Peak DSP was 1,354,037,961 bytes and health
counters zero; hardware backlog/worker fields were unavailable. The sealed
20,072-record trace had zero drops and closed lifecycles, while conservative
counter completeness stayed false for 188 records. Longest qualification was
2.36 seconds. Body qualified C/N0 median was 30.44285 dB-Hz; posthoc known-truth
frequency error median/p90/max was 0.005519/0.020982/0.085170 Hz against `U`
0.149679/0.662941/1.864795 Hz. All compared centers were inside `U`, descriptive
correlated evidence rather than confidence calibration. No full 128-second
symbol support or PHY deficit test completed.

The [original R8 EME failure](../local-evidence/helper-v16/normal-acceptance-private/r8-final-controlled/r16-r8-eme15k-128s-on-v1/sealed-ordinary-summary.json)
(`e7e4e5ba`) remains failed/incomplete at 300.7176 wall seconds, with 140.134
captured media seconds and 2.5937568 body-feed CPU/media. R11 advanced less
media, so no grid/cache work benefit is claimed. The [earlier R6 startup failure](../local-evidence/helper-v16/normal-acceptance-private/final-controlled/r16-eme15k-128s-on-v1/sealed-metrics-summary.json)
and [private 0.03 Hz admission diagnostic](../local-evidence/helper-v16/normal-acceptance-private/precision-admission-gate-controlled/r16-eme-useful-precision-gate-v1/sealed-diagnostic-summary.json)
retain their failed/incomplete scopes. The 1.907 dB PHY deficit remains untested.

The [precision control receipt](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/checks-review-r4/execution-result.json)
retains abrupt step/slope/clock failures with 6/19/5 escaped qualified polls.
The 0.005 Hz term is an engineering allowance. Private [shared-covariance](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/shared-covariance/checks/execution-result.json),
innovation-coast and [shortened-context](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/context-length-r1/checks/execution-result.json)
variants remain rejected/deferred, not physical impossibility proofs. A [private rectangular-400 ms discriminator](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/rect-period-r1/checks-repair1/execution-result.json)
(`6397b3d6`) failed three causal/domain cases despite 60 real-PCM model checks
passing. Its laboratory truth-frequency ceiling reached median `U` 0.01836 Hz/
final 0.01259 Hz at six seconds in a 30.76 dB-Hz/+100 ppm control, while the
causal full-waveform case could not bootstrap an FLL interval. This was not
integrated and is not a physical precision limit. The FFT reference experiment
remains private; work-based CPU admission is the current explicit opt-in policy
described above, not a private-only proposal.
The later [private periodic-FFT real-bound control](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/rect-real-bound-r1/checks/execution-result.json)
(`ce5af4aa`) passed its component checks: the 30.76 dB-Hz case had median `U`
0.0443 Hz, final 0.02617 Hz and median error 0.000576 Hz over 33.6 seconds, at
about 0.0009 measured component CPU/audio. Other 40/75 dB-Hz and callsign cases
were worse than the stable kernel, so it was not integrated. This is neither
ordinary receiver CPU nor a physical precision bound. The later [private
past-frequency steering receipt](../local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/rect-past-steer-r1/checks/execution-result.json)
(`582f7ba5`) passed 27 component cases and DATA in 18.79 seconds. Its 30.76 dB-Hz
control reduced median `U` 0.04428→0.02719 Hz, with final 0.01615 Hz, median error
0.000571 Hz and zero observed escapes over the 33.6-second control. It is
not production-eligible: unknown seven-symbol metadata, an unqualified evolution
prior and worse strong 40/75 dB-Hz cases remain; changed-model cases withdraw
without recovery. It was not integrated. A separate conditional coarse WLS/L1
branch was rejected on numerical-proof grounds, not as a physical limit. New
private precision proposals do not alter the current runtime feature set.
The original
compact [test compile failure](../local-evidence/helper-v16/compact-first-symbol-continuation/focused-r1/execution-result.json)
and [Auto wrapper-control failure](../local-evidence/helper-v16/compact-first-symbol-continuation/focused-r2/execution-result.json)
remain individually retained; their [focused repair](../local-evidence/helper-v16/compact-first-symbol-continuation/focused-r3/execution-result.json)
did not weaken payload/progress/END assertions or change production policy.

Full current contract, native and sanitizer coverage, larger framed transfers,
complete EME reception and cable acceptance are incomplete. The 0.6 receiver
budget is an Automatic backend-selection objective, not a whole-GUI CPU ceiling.
No release certification, universal CPU gain or full feature completion follows.

### Earlier ideal forecast versus actual tone precision — V14

V14 changes Planner math/UI wording only; runtime core `7e7f332c` is unchanged.
The [build receipt](../local-evidence/helper-v14/build-r1/execution-result.json)
records GUI build 10.67 seconds, focused-test build 3.52 seconds and three
focused checks passing in 1.75 seconds, with unchanged pins and all processes
reaped. GUI hash is `49cf3aef7589ecbe02a6e43b630078fd05db62ceb3567ec0e25500c713d08446`.
There is no new audio or full-regression result.
The ideal contiguous endpoint covariance corrects forecast `U` from
0.950280808 to 0.0604611302 Hz at C/N0 30.0206 dB-Hz after 7.7 seconds, retaining
configured process terms and floors. It assumes 384 uninterrupted accepted
20 ms observations and zero excess residual, not a measured metadata waveform.
Runtime already uses actual snapshot `U` (only a 1e-6 Hz numerical floor) plus
necessary original LF/UTC geometry. The no-FHSS 300 ppm LF term remains
±0.16875 Hz until clock is independently qualified. The
[corrected dump](../local-evidence/helper-v14/planner-precision/corrected-dump/estimate-dump.log)
(`66685d42`; receipt `10ba8d1d`) has ideal RF/LF half-extent 0.2292111302 Hz,
103 pairs retaining all nine LF centers, and modeled receiver CPU/audio 5.797316
versus the earlier conditional 1013 pairs/38.609782. The entire ordinary baseline
is identical; 13 initial epochs, 33 admissions and 78.7978-second cohort lifetime
remain unchanged. These are conditional model outputs, not measured work gains.

The [tone-precision receipt](../local-evidence/helper-v14/tone-precision/execution-result.json)
(`127c4dbf`) binds five synthetic 12-second actual-Generator cases, all late
polls qualified with zero observed interval escapes; compilers/run were reaped.
At expected C/N0 30.0206, HF metadata plus data had median/p90/maximum `U`
0.566217/0.626/0.658 Hz and maximum truth error 0.5425 Hz. CW medians were
0.155–0.174 Hz and GPSDO metadata/data 0.4196 Hz. Adding ordinary payload made
almost no difference to metadata-only precision in this control. These bounded
component results do not calibrate confidence, prove normal 0.06 Hz precision,
measure decoder CPU or qualify the weak RF link. The same ideal assumptions
cannot be assigned to interrupted metadata observations or empirical excess.
The [private GLS prototype](../local-evidence/helper-v14/gls-precision/execution-result.json)
was not integrated: its legacy weak-EME heuristic guard failed (`U` 0.14094 versus required
>0.2 Hz; later legacy controls unreached), while metadata `U` improved only
0.5662→0.5322 Hz with roughly 9 KiB extra storage. The earlier V13 GPSDO prefix
failure and unreproduced no-pending report remain unresolved; prior binary-bound
cable results are not relabeled current GUI tests.

### Preceding V13 source, reentry and focused cable evidence

The [final build receipt](../local-evidence/helper-v13/build-r2/execution-result.json)
(`7b887940`) records GUI build 63.98 seconds, test build 13.09 seconds and eight
focused checks in 6.971 seconds, all exit zero. GUI helper status, debug-repeat,
actual Session gap routing, clock refresh and CPU-scope rendering also passed.
Inputs remained unchanged during execution and owned processes were reaped.
[Binary bindings](../local-evidence/helper-v13/build-r2/production-binaries.json)
pin GUI `4d0a71d0c61cb83d92d2298303254cb7b44fe2b827ea3a6098477f9bb950f5db`
and core archive `7e7f332ce636e71236f564c895f1671d5ef8780d7b6be5bfb441b5177dd3b1e3`.
The preceding build-r1 receipt and its passing checks remain source-bound,
not a substitute for current cable results.

The [two-cycle cable result](../local-evidence/helper-v13/live-runs/helper40-repeat-r2/result.json)
(`028ee0cf`, same GUI) exited zero after 204.917 seconds. Both exact one-bit
payloads completed on IDs 1 and 2 with ordinary END; input overruns, drops and
resets were zero. ID 1 first appeared already complete at 48.0116 seconds.
ID 2 was pending at 137.407 then complete at 168.679 on the same ID; first-row
pending coverage was not observed and is not claimed. Process CPU was 154.929014 seconds
for 203.315196 observed audio seconds, or 0.76201394 total GUI CPU/audio. Peak
backlog was 3.125 seconds and final zero; recorded FFT jobs were 10,276.
Inputs matched and owned groups/children were empty at completion.

Its [low-volume trace](../local-evidence/helper-v13/live-runs/helper40-repeat-r2/reference.jsonl)
records reentry at sequence 4626, with 77 cold rows and none retired. Raw clock
qualification at sequence 4660 was 0.852507 seconds later; joined CLOCK refresh
at sequence 4663 was 0.918698 seconds after reentry, about 66 ms after
qualification. Bank sample 675,160 and RF/NCO remained unchanged. Observed raw
clock support 5,277,072–5,337,743 contains queued raw point 5,298,000; refresh
end 5,341,200 at 48 kHz gives about 0.072 seconds age. This establishes that the
metadata-refresh mechanism executed on captured support, not a new input
frontier or a frequency/phase/thermal lease.

The [preceding three-cycle result](../local-evidence/helper-v13/live-runs/helper40-repeat-r1/result.json)
(`fef55cdc`, GUI `856d46a2`) retains exit 1 at 300.222 seconds: cycle starts
0.509/108.951/218.148 seconds and roughly 92-second helper output mean the last
tail needed about 311 seconds. All three distinct payload rows 1/2/3 completed
exact one bits with ordinary END and no missing/unexpected bits; rows 2/3 had
observed pending-to-complete identity. This deadline setup failure is separate
from actual reentry lag: 18.125 seconds peak backlog, 1.232123 total GUI
CPU/audio and 29,015 recorded jobs, with zero input health faults and final
backlog zero. The N3/N2 cycle counts and tracing differ, so the lower observed
N2 backlog is not a universal or matched percentage gain. No retired-row
replacement policy has been implemented.

The [quiet GPSDO/helper-ON result](../local-evidence/helper-v13/live-runs/gpsdo-helper40-long-r2/result.json)
(`2c263669`, same `4d0a71d0` GUI) failed with exit 1. At 119.759 seconds,
ID 1/revision 7 had a wrong seven-bit prefix; revision 8 completed the correct
`01001101`/ordinary END at 140.971 seconds. Final correctness does not erase
the prefix failure or make this a functional pass. Source review locates
revision 6→7→8 in Live owner/merge replacement, which publishes a selected
candidate's accumulated post-XOR prefix; the GUI does not synthesize bits.
ReceptionHistory tracks neither bits nor prefix agreement. The quiet log cannot
identify the wrong candidate or establish root cause. No arbitration policy
change is justified or made; a bounded joined commit/before-XOR wire trace is
needed to discriminate the fault. Inputs matched and processes were reaped,
with zero overruns, drops and resets, peak backlog 0.725 seconds and final zero.
Process CPU was 132.860561 seconds over 194.440196 observed audio seconds,
0.6832978 total GUI CPU/audio; wall time was 195.412498 seconds, with 8,267
recorded FFT jobs. The OS-counter body window, GUI 50.763–109.647 seconds,
used 44.35 CPU seconds over 58.875 audio seconds, or 0.7532909 CPU/audio.
Those cost measurements remain valid observations of a failed control, not
performance or functional acceptance.

The [quiet crystal/helper-ON result](../local-evidence/helper-v13/live-runs/helper40-long-r2/result.json)
(`e2e22889`, same GUI) exited zero with exact `01001101` and ordinary END at
141.654 seconds. ID 1 retained correct pending prefixes of 3/5/7 bits at
48.424/79.83/110.358 seconds, then completed; no eight-bit pending poll is
claimed. Input overruns, drops and resets were zero, with no extras/missing
bits. Process CPU was 116.089419 seconds over 195.215196 observed audio seconds,
0.5946741 total GUI CPU/audio; wall time was 196.021126 seconds. Peak backlog
was 0.275 seconds, final zero and FFT jobs 6,321. The OS-counter body window,
GUI 50.7978–109.562, used 38 CPU seconds over 58.775 audio seconds, or
0.6465334 CPU/audio. Inputs matched and owned groups/children were empty.

| Current quiet case, same `4d0a71d0` GUI | Prefix/END outcome | Total GUI CPU/audio | Body CPU/audio | Peak/final backlog, seconds |
| --- | --- | --- | --- | --- |
| Crystal/ic-7100, helper ON | Pass: correct 3/5/7 pending prefixes, then exact eight-bit END | 0.5946741 | 0.6465334 | 0.275/0 |
| GPSDO-configured, helper ON | Fail: transient wrong seven-bit prefix, then correct final eight-bit END | 0.6832978 | 0.7532909 | 0.725/0 |

The [frozen current comparison](../local-evidence/helper-v13/live-runs/current-comparison.json)
has SHA-256 `fdb7e0d8cdaf3719c05a59ba1bee564df69415fb5a78a5eaf7235e07ae768b3c`.
Both use automatic 50% workspace rather than fixed bytes and different keyed
dates/waveforms. Body median reported helper C/N0 was 49.071252 dB-Hz for
crystal and 49.868308 for GPSDO, about 19 dB above the configured 30.0206 dB-Hz
budget; the earlier 60.974331 dB-Hz measurement belongs only to old r7.
Physical cable capture did not inject the configured RF loss/noise. These
whole-GUI costs do not isolate ordinary receiver processing or establish a
universal causal oscillator-cost/CPU gain. The GPSDO control stays failed.
All audio/monitor processes were reaped. The reported intermittent missing
pending row after the helper tail remains unreproduced and unresolved; no
full regression, sanitizer, long framed, weak RF or EME qualification follows.

The [R2 cold-bootstrap receipt](../local-evidence/helper-v13/cold-pullin-r2/execution-result.json)
(`2228ac0d`) uses four exact-adjacent, mutually consistent fine observations to
recenter only the NCO. They do not enter fit history or qualify synchronization;
ordinary 16-observation/0.3-second qualification, model/noise and expiry gates
remain. Tracker storage is 88,272 bytes. In the actual-metadata +30 Hz component
case, translator admission changed from 7.525 to 0.675 seconds and whole-waveform
coverage from 61.86% to 100%, with 0.0084 dB coherent loss. Eight seed/dwell cases
qualified in 0.700–0.875 seconds with truth inside their intervals; all old
tracker controls passed. This is not ordinary decoding, CPU, physical END or
hardware qualification. The [initial widened-gate proposal](../local-evidence/helper-v13/cold-pullin-r1/execution-result.json)
(`8425cc9d`, proposal `9891340a`) remains failed: it admitted a biased reference
and regressed coherence. R2 is a separate repair, not a passed original run.

The [actual Session routing regression](../local-evidence/helper-v13/realtime-gap/root-setup-r4/execution-result.json)
(`c1a7e6b9`) reproduces the baseline's lost qualified real-time context and passes
after routing repair, retaining exact core prefix/END checks. Earlier passband
setup failures remain preserved. This component defect is not established as
the cause of the user's missing-pending report after the helper tail.

The new CLOCK-only refresh uses the shared raw/joined clock builder, same
generation/frontend serial/raw rate/current captured end, 0.55-second freshness
and original LF bounds, at post-loan/pre-borrow boundaries. Valid queued clock
is retained when current clock is absent. Queued RF/U, NCO, PCM, phase, epoch
and frontiers are unchanged. Controlled guard/selector 9→3→1→restore and actual
Session raw/joined-clock prefix/END/loss checks passed. The quasi-stationary
aperture-mean clock assumption remains; frequency is not made dependent on
punctuation. Verbose formatting is filtered only for debug repeat > 1; normal
verbose behavior is unchanged. This contributes a tracing-scope difference.

Planner's ordinary cohort lifetime changed from 47.51 to 78.8 seconds, with a
reference ordinary-receiver estimate 0.093589 to 0.183126 processing seconds
per audio second. The fixed-256 MiB actual-receiver two-hop regression checks
retirement timing; it is not end-to-end CPU calibration. UI labels ordinary
receiver CPU exclude helper tracking/conversion, audio, TX and GUI work. The
baseline and conditional frequency-only estimate retain their separate scope.

Three new-turn short trials still use preceding GUI `18c77619`: crystal/helper
ON [r8a](../local-evidence/helper-v12/live-runs/helper40-short-r8a/result.json)
(`f90dc36d`) and [r8c](../local-evidence/helper-v12/live-runs/helper40-short-r8c/result.json)
(`9fc0c1e1`) used 0.5214 and 0.4593 total CPU/audio; GPSDO/helper ON
[r8b](../local-evidence/helper-v12/live-runs/gpsdo-helper40-short-r8b/result.json)
(`d1873f29`) used 0.5803. All recovered the exact one bit/ordinary END with zero
health faults, peak backlogs 0.275/0.275/0.75 seconds respectively and final
zero. They do not reproduce the intermittent report or isolate oscillator cost.
The earlier eight-bit GPSDO/helper-OFF versus crystal/helper-ON body comparison
(0.5196 versus 0.6796) also differs in capture rate (8 versus 48 kHz) and total
GUI overhead. The completed current comparison is recorded above; the GPSDO/helper-ON
prefix failure remains failed.

### Preceding bounded frequency history and strong-cable results

The [build-r7 receipt](../local-evidence/helper-v12/build-r7/execution-result.json)
(`1abc1ef8`) records successful GUI build (1.8177 seconds), test build
(3.4688 seconds), seven checks (6.4199 seconds) and GUI helper status (0.010
seconds), with unchanged execution inputs and reaped processes.
[Binary bindings](../local-evidence/helper-v12/build-r7/production-binaries.json)
pin GUI `18c77619da2aca602c2788b9b499c5602076b61bbef0c276d21014c059ced24b`
and core archive `b052fa02`.

The [r7 helper cable result](../local-evidence/helper-v12/live-runs/helper40-long-r7/result.json)
(`e6319b95`, same GUI `18c77619`) exited zero, with unchanged inputs and reaped
processes. Exact `01001101` progressed on stable ID 1 through 3, 5 and 7
pending bits, then eight-bit completion, with valid prefixes, ordinary END, no extras/missing bits,
no late/failure status and explicitly zero overruns, drops or resets. Process
CPU was 123.640958 seconds over 194.165196 observed audio seconds, or 0.6367823
CPU/audio; wall time was 195.419154 seconds and peak RSS 161,988 KiB. Peak
backlog was 0.75 seconds, final backlog zero and first completion 140.963 seconds.

The OS-counter body window, GUI seconds 50.7083–109.49, used 39.96 CPU seconds
over 58.8 audio seconds: 0.6795918 CPU/audio. All 59 body live polls had clock
narrowing active. The run kept up in this strong-loopback case. The earlier
comparison against a supposed strict 0.6 total-GUI ceiling was incorrect;
these CPU/audio ratios do not isolate the receiver selection objective.
Its 6,902 recorded native FFT jobs compare with 28,371 in pre-cadence r4 below,
75.67% fewer in these runs; process CPU was about 62.7% lower. These are
source-bound executed counters/process measurements, not a universal work,
probability or sensitivity gain.

Median measured helper C/N0 was 60.974331 dB-Hz. The configured 40 W/180 dB/
−164 dBm/Hz budget predicts 30.0206 dB-Hz, about 30.95 dB weaker. Physical cable
capture does not inject the software path-loss/noise settings: this establishes
an ordinary Auto path and work reduction under a qualified strong reference,
not that weak RF link, EME, full framed payloads or GPSDO substitution. The
same-GUI helper-off eight-bit GPSDO comparison is complete as recorded below.
Prior high-cost observations and original failed receipts remain retained below.

The [GPSDO-configured helper-off result](../local-evidence/helper-v12/live-runs/gpsdo-long-r7/result.json)
(`1023517b`, same GUI `18c77619`) exited zero with exact eight bits/ordinary
END, stable prefixes, no extras or missing bits, no late status and explicitly
zero overruns, drops or resets. It used 59.925182 CPU/141.908700 wall seconds
over 140.617157 observed audio seconds: 0.4261584 CPU/audio overall. Peak backlog
was 0.15 seconds and final backlog 0.075 seconds while capture remained open
until normal close; first completion was 139.757 seconds and peak RSS 125,720
KiB. Inputs were unchanged and owned processes reaped.

Its OS-counter body window, GUI seconds 50.6119–109.384, used 30.54 CPU seconds
over 58.775 audio seconds: 0.5196087 CPU/audio. Thus helper body CPU/audio
0.6795918 is about 30.8% above this reference, not identical to it. Total GUI
process CPU does not isolate ordinary decoder or optional overhead, and capture
rates differ (8 kHz helper OFF versus 48 kHz helper ON). No assumed overhead
subtraction or whole-process ratio establishes receiver-budget conformance. The helper-off
FFT counter is uninstrumented: its recorded zero is unavailable work evidence,
not zero actual FFT execution.

The frozen [measured comparison](../local-evidence/helper-v12/live-runs/measured-comparison.json)
has SHA-256 `c84352c57dfc0c0362fb0f66a4e10fcfdb718b2ca2d161fcb2869f1d94b81c25`.
It retains the source/binary, progress, status and measurement scopes for these
cases, including pre-cadence r4 body CPU/audio 2.8526316 versus r7 0.6795918.
All owned audio/build/private processes were reaped. This is a scoped manual
preview, with full regression, sanitizer, long framed transfers, weak RF and
128-second EME still unqualified; it is not overall task completion or release
certification.

The frequency history is now at most 384 numeric records/eight seconds, rather
than 128/three seconds. Current-date linear-trend/covariance, process/excess,
slew, innovation and 0.2-second expiry rules remain; the raw-PCM ring is
unchanged. The tracker is 88,152 bytes, exactly accounted in helper workspace.
Planner's ideal assumption becomes 384 accepted 20 ms observations within
eight seconds; metadata/rejections can prevent that maximum. The estimate
remains frequency-only, with full LF/UTC/storage geometry and its overhead/
clock-mask/lock-probability exclusions unchanged.

The [matched component receipt](../local-evidence/helper-v12/frequency-history-r2/second-check/execution-result.json)
(`139dedb8`) records all four compile/runtime commands exiting zero and reaped
children. Actual-Generator frequency half-width median/p90 changed from
0.0318277/0.0356229 Hz to 0.0212528/0.0221695 Hz, with maximum error
0.02755 to 0.00997 Hz and zero observed interval escapes in both cases.
Corrected-post-NCO selector-only counterfactual mean count changed from five
to 3.011, with about 0.65% of cases still wider than three. Raw-passthrough
geometry is reported separately (new mean 3.14467, p90 four). None of these
counts is executed receiver work or a CPU claim; they are not a general
confidence guarantee. The earlier 256-record/six-second candidate remains
separately retained in `frequency-history-r1/`.

The original [r6 check failure](../local-evidence/helper-v12/build-r6/execution-result.json)
is retained: GUI/test compilation passed, but `helper_reference_search` still
asserted the old at-most-three-second observation aperture. The accepted
test-only repair checks [7.6, 8] seconds around the new ideal 7.698 seconds;
the r7 focused pass does not erase the original failure.

### Preceding cadence repair and completed cable result

The [build-r5 receipt](../local-evidence/helper-v12/build-r5/execution-result.json)
(`724344a5`) records successful GUI build (3.0695 seconds), test build
(2.818 seconds), seven checks (5.7199 seconds) and GUI helper status (0.010
seconds), with unchanged execution inputs and reaped processes.
[Binary bindings](../local-evidence/helper-v12/build-r5/production-binaries.json)
pin GUI `665781bcd799aeb62c6a645bc4039a11471e70dff46693e243dc1c570a442b81`
and core archive `9a45312e`.

Its completed [eight-bit r5 result](../local-evidence/helper-v12/live-runs/helper40-long-r5/result.json)
(`bc101c31`) exited zero with exact eight bits, ordinary END, no health faults
or reported late decisions. It used 163.627883 CPU/195.193789 wall seconds over
194.265196 observed audio seconds: 0.842291 CPU/audio overall, with reported
0.9789 in the 50–110-second body window. Peak backlog was 0.875 seconds and
final backlog zero; the FFT-job count was 11,004 and first completion 141.994
seconds. This remains a whole-GUI cost observation; its earlier 0.6-budget
failure label used the wrong scope. It does not establish isolated receiver
cost, current-source performance or GPSDO substitution.

The preceding [r4 cable result](../local-evidence/helper-v12/live-runs/helper40-long-r4/result.json)
(`d0284eec`, GUI `4a7b0bcd`) exited zero for exact eight-bit reception,
ordinary END and zero health faults, with the following high whole-process cost:
332.043315 CPU/195.798421 wall seconds over 194.315196 observed audio seconds,
or 1.708787 CPU/audio. Peak backlog was 8.85 seconds and final backlog zero;
the recorded FFT-job count was 28,371 and first completion 142.13 seconds.
The earlier budget-failure interpretation is corrected above; raw measurements
and functional/failed receipts remain unchanged.

An actual encrypted-Generator/ramp reproduction at public zero key and epoch
1793050572 reproduced a false C2 crossing near the 400 ms boundary, poisoning
the six-second alias fit. The
[matched component receipt](../local-evidence/helper-v12/punctuation-clock-r4-actual-waveform/first-check/execution-result.json)
(`c6a26915`) retains the old runtime's exit 1 (`actual encrypted helper boundary
poisoned the clock aperture`) and the repaired runtime's exit 0, with all
children reaped. Qualification rose from 94.3191% to 100%, with unchanged
maximum interval half-width 5.88554 ppm; the extra off-cadence/fresh-support and
previous controls passed. This is not a new cable-performance result.

The applied fresh-clock gate checks all integer 400 ms aliases under the broad
±1% operational bound, including both dated radii. Live separately intersects
the original configured LF domain. The gate rejects a crossing
without changing its date or interval. The gate stops at the ordinary 550 ms
freshness expiry, preserving existing reseeding. Tracker storage stays 75,864
bytes. No new decoded timecode, phase authority or receiver is introduced.

### Preceding narrow marker repair — component evidence

The [build-r4 receipt](../local-evidence/helper-v12/build-r4/execution-result.json)
(`02f68d7b`) records successful GUI build (3.468 seconds), test build (2.669
seconds), seven checks (3.1695 seconds) and GUI helper status (0.010 seconds),
with reaped processes and unchanged execution inputs.
[Binary bindings](../local-evidence/helper-v12/build-r4/production-binaries.json)
pin GUI `4a7b0bcd07965622bb35668e3c9488ae302865872bd95f40db7e3e4a18c22c22`
and core archive `83128eb8`. Its eight-bit cable run and matched GPSDO comparison
have no result recorded here yet.

The integrated marker projection uses an 8 ms Hann window in eight staggered
1 ms branches, preserving the real-PCM image fit. Exact overlap-row bounds and
finite-flank-noise correction bound the model; an unsigned absolute 80 ms guard
fixes duplicate-crossing admission. The tracker is now 75,864 bytes, rather
than 71,968. The
[fifth component receipt](../local-evidence/helper-v12/punctuation-clock-r3-narrowband/fifth-check/execution-result.json)
(`b563dbb5`) records passing compile/runtime checks and reaped children.
All four ±150/±300 ppm cases retain initial qualification/truth inside their
interval after about 2.3 seconds, with corrected 19–28 ppm half-widths; after
6.3 seconds the 20 ppm target passed at 4.96–5.51 ppm. Negative, loss and huge
origin controls also passed. Earlier failures in the same evidence directory
remain retained; the duplicate admission was a causal defect, not merely
insufficient warm-up.

In the controlled 450 Hz ordinary-payload/10 dB helper-to-data component case,
old clock qualification was 66.7%, median half-width 84.35 ppm and 90th
percentile 106.89 ppm. The repaired component qualified 100%, with median
5.20 ppm, 90th percentile 5.48 ppm and maximum error 1.65 ppm. These are
component interference results, not a completed new cable, sustained CPU-budget
or general confidence qualification.

### Earlier v12 clock-rate source and cable results

The [build-r3 receipt](../local-evidence/helper-v12/build-r3/execution-result.json)
(`6c43f8e2`) records successful application build (14.72 seconds), test build
(11.47 seconds), seven focused checks (2.57 seconds) and GUI helper status.
Inputs remained unchanged during execution and owned processes were reaped.
[Binary bindings](../local-evidence/helper-v12/build-r3/production-binaries.json)
pin GUI `70fc25b06fde343599b4a14ee9d313fd4475b41c37d629ed8669bde15b1f7ee3`
and core archive `bd8a22c8`.

The fixed 71,968-byte tracker adds a streaming 400 ms reversal clock-rate
estimate with at most 16 markers/six seconds and about 96 ms of recent phasors,
using the actual real-PCM image model. It measures a conditional aperture-mean
clock rate under the ordinary constant-clock, locally stationary scored-aperture
model, not epoch, phase, timecode, thermal stability or future lock authority.
There is no known TX date, GPS dependency, PCM time warp or new decoder.
Per-profile eligibility requires continuous zero-UTC steering, no FHSS or
Symbol_Burst and T ≤ 16 seconds. Qualified clock intervals intersect original
LF cells and the RF strip in the same bank, retaining the full residual
catalogue. Clock loss immediately restores the full LF domain; frequency
qualification remains independent.

Focused tests include ±150/±300 ppm clock cases whose approximately 2.3-second
aperture intervals have 17–19 ppm half-widths, weak/false-marker declines and
native/compact 24-bit frequency/clock paths with exact full pending progress,
identity, ordinary END and final helper loss. These are conditional component
results, not general false-alarm, weak-EME or current cable qualification.
Planner now preserves its ordinary baseline and adds a separate frequency-only
conditional CPU curve using expected helper C/N0, 128 steady 20 ms observations
and zero excess model noise. It retains full LF/UTC/workspace geometry and
payload SNR/Rate/PHY, excludes tracker/frontend overhead and clock-mask gains,
and has no runtime-calibrated lock probability or end-to-end CPU guarantee.

The original [r1 compile failure](../local-evidence/helper-v12/build-r1/execution-result.json)
for the missing UTC-relation declaration/include remains retained. In
[r2](../local-evidence/helper-v12/build-r2/execution-result.json), all seven
focused checks passed but GUI status failed its stale correction-state flag
expectation; that failed run is preserved after the accepted source repair and
r3 pass.

The first [helper40 cable result](../local-evidence/helper-v12/live-runs/helper40/result.json)
(`ed0522b3`, GUI `70fc25b0`) exited zero with the exact one source bit and
ordinary physical END. It used 73.35876 process CPU/96.713782 wall seconds over
95.890196 observed audio seconds, or 0.7650288 CPU/audio. Peak backlog was
1.15 seconds and final backlog zero; input overruns, drops and resets were
explicitly zero. Clock assistance was observed; the recorded native FFT-job
count was 4,837, and first observed completion was 49.7092 seconds. Compared
with the preceding d441 single-case baseline below, this is measured progress,
not isolated receiver-budget or general real-time qualification.

The subsequent [eight-bit result](../local-evidence/helper-v12/live-runs/helper40-long/result.json)
(`abd864aa`, same `70fc25b0` GUI) exited zero with exact `01001101`, stable
prefixes, ordinary END and no unexpected bits. It used 327.618353 process
CPU/195.983195 wall seconds over 195.215196 observed audio seconds, or
1.678242 CPU/audio. Peak backlog was 2.85 seconds, final backlog zero and
input overruns, drops and resets explicitly zero; the recorded FFT-job count
was 28,595 and first completion 142.002 seconds. This is high total-GUI cost,
not an isolated test of the receiver selection budget. Wider payload clock
intervals (about ±80 ppm versus ±3 ppm in the clean helper tail) motivated the
component repair above; no repaired-source cable gain is claimed yet.
Long text, attachments,
128-second EME, folded-input full upstream
correction and waveform phase assistance are not newly qualified.

### Preceding 40 W cable preview — narrow evidence

The actual tested command enables a 40 W helper: Rate 36, independent crystal
audio/ic-7100 RF at 30 MHz, Interleave 10, FHSS off, 70 ms audio error and
Live/duplex. It is not a helper-off control. The quiet cable pair binds the
[preceding GUI](../local-evidence/helper-v11/live40-before-quiet/result.json)
(`b075fc91`, GUI `16668651`) and
[delivered GUI](../local-evidence/helper-v11/live40-delivery-quiet/result.json)
(`7f390b2b`, GUI
`d441db117baf5bfc09b9ecabb09204de618f57f464ffc0be5410cfd3e0ef725c`).
Both runs exited zero with the exact one source bit, ordinary physical END and
no unexpected bits, input overruns, drops or receiver resets.

| Observation | Preceding GUI | Delivered GUI |
| --- | --- | --- |
| Process CPU seconds | 378.492347 | 341.257965 |
| Wall seconds | 96.1770 | 96.1653 |
| First observed completed bit, seconds | 63.8757 | 55.7347 |
| Peak/final capture backlog, seconds | 16.35/16.35 | 9.85/9.85 |
| Aggregate CPU seconds per observed audio second | 3.9919 | 3.5973 |

The delivered run still does not keep up: backlog remains 9.85 seconds and
total GUI CPU/audio remains high. This same-case observation is
neither overall goal completion, GPSDO substitution, general real-time
qualification nor an RF sensitivity/precision claim. The final UTC guard is
included: both paths preserve the uncertainty term `U + |H| C (1 + A)`, with
frequency uncertainty `U`, helper frequency `H`, declared UTC steering bound
`C` and physical audio-clock bound `A` (clock bounds are fractional). The
original shared-radio `C = 0` case is retained.

The separately bound [precursor trial](../local-evidence/helper-v11/live40-final-quiet/result.json)
(`72f7ff33`, GUI `7bafc96a`) used 320.117892 CPU/96.6389 wall seconds and ended
with 8.25 seconds backlog. It is not the delivered binary's result. The guard
is equivalent for this HF zero-UTC configuration, but legitimate metadata and
random trial realizations differ; these runs supply no statistical gain claim.

The corrected preview applies RF masks to executed work in the existing bank
while retaining original LF values/cells, uses the direct `D − H`
residual-clock coupling, fixes stale reuse at the 128-entry observation FIFO
wrap and accounts for frequency-measurement covariance without removing
model/excess-noise floors. Folded mono is labeled frequency-origin-only;
full upstream correction is unavailable there. Phase/punctuation assistance is
inactive. Long text, attachments and 128-second EME are unqualified this iteration.

The final [build-r5 receipt](../local-evidence/helper-v11/build-r5/execution-result.json)
(`086ca19d`) records six short checks plus GUI helper status passing, with
unchanged inputs during execution and reaped processes. Native/compact checks
retain exact 24 bits, full pending progress/identity, ordinary END and final
helper loss. The native fixture observed 38 actual competing fits and 228
skips. Earlier SF8 setup and compact warm-skip coverage failures remain in
[build-r2](../local-evidence/helper-v11/build-r2/execution-result.json) and
the retained private diagnostics; they are not relabeled passing original runs.

The earlier sandbox [GUI attempt](../local-evidence/helper-v11/live40-before/result.json)
and [controller attempt](../local-evidence/helper-v11/live40-controller-before/result.json)
could not access audio and provide no reception qualification. The
[85-second GUI trial](../local-evidence/helper-v11/live40-gui-before/result.json)
recovered the one bit and physical END at 66.9981 seconds, but retained exit 1
because its deadline preceded the conservative helper post-tail (about 92.4
seconds); it is not an acceptance pass. An intermediate trace-enabled
[GUI trial](../local-evidence/helper-v11/live40-gui-after/result.json), binary
`16ae5a92`, used 388.012682 process CPU seconds but capped its 256 MB trace.
That incomplete trace is excluded from numeric-work proof.

### Initial manual preview checks — preceding build

The [r4 build receipt](../local-evidence/helper-v10/manual-gui-build-r4/execution-result.json)
(`a6ab1627`) records the GUI rebuild passing in 64.57 seconds. Frontend, tracker
and Planner checks passed, while dispatch failed an ambiguous-arrival fixture:
matched OFF/ON both selected the same seven-bit alias. The minimal exact-arrival
test repair retained payload/progress/END predicates. The subsequent
[r5 receipt](../local-evidence/helper-v10/manual-gui-build-r5/execution-result.json)
(`6a5b509e`) records all four short checks passing in 1.535 seconds, including
exact eight-bit pending/completed OFF/ON dispatch, plus GUI helper status in
0.010 seconds. All stages exited zero with owned processes reaped, unchanged
inputs and no added inputs during execution. Its
[input bindings](../local-evidence/helper-v10/manual-gui-build-r5/inputs.json)
(`a4dbe5a0`) and
[binary bindings](../local-evidence/helper-v10/manual-gui-build-r5/production-binaries.json)
(`872ababc`) pin GUI
`166686512720582de0474b421bf474daec62c8b704419c831b592197ee503c81`
and core archive `5613347f`. R5 changes the dispatch test only; the GUI is
unchanged from r4. The original dispatch failure is retained.

The separate [continuous-NCO smoke receipt](../local-evidence/helper-v10/manual-smoke-nco/execution-result.json)
records a passing 0.376-second check. A strong B tone diagnostic measured
5305.523 ± 0.04183 Hz against nominal 5275 Hz, at about 58 dB-Hz C/N0.
The B waveform folds around DC: `reference_passthrough` preserves its PCM and
fixed helper notch, setting the first ordinary demodulator reference to the
measured helper offset plus `(data frequency − helper frequency)` times each
candidate's fractional audio-clock error. Positive-passband HF instead uses
the continuous upstream NCO. Both use one ordinary bank. Earlier
[B preview](../local-evidence/helper-v10/normal-b12-preview/execution-result.json)
and [r2](../local-evidence/helper-v10/normal-b12-preview-r2/execution-result.json)
waiting/no-feed failures remain retained; folded-input treatment was corrected
before r3, not reported as a passed initial run.

The [B12 r3 receipt](../local-evidence/helper-v10/normal-b12-preview-r3/execution-result.json)
(`f299e170`) retains exit 1. Its complete trace reports all 12 wire/source bits,
full pending progress, stable identity and ordinary physical END, with no
overruns, drops or resets. The strict failure is solely the old
`primary_assisted=0`/cohort-work classification: zero conditional-cohort jobs
are expected in this new single-ordinary-bank route. It is not relabeled a
current whole-fixture pass. The locked receiver initially used 19 pairs versus
2,075 baseline pairs; this is geometry, not a CPU-gain measurement. The
trace-instrumented process used 65.508563 CPU/15.925781 wall seconds for about
26.67 captured audio seconds. That total-GUI ratio does not test the configured
receiver selection budget. No CPU,
real-time, current cable, long-payload or broad-regression qualification follows
from these abbreviated checks. Metadata/punctuation timing assistance and
128-second coherence are not newly qualified.

### Pre-preview correction — superseded

The wide-capture/compact-receiver correction is under development from
`efa00b0c63f66b3911d6e392ff42c9740fce6aa7`. Last coherent integration v26 retains
v25 exact continuation-row reuse and resolves five focused checks across those
two runs after a test-only progress-oracle repair. This is not full normal
qualification. The latest B12 diagnostics report exact full wire/source,
retained pending identity and ordinary END, but their unchanged strict helper
gate still fails the assisted-provenance tag. The v27 build failed its auxiliary
storage bound; no newer GUI/runtime result is qualified. Prior A/EME startup
passes cover frequency bootstrap only. A supported v21 private continuous
256-bit fixture completed both legs; its posthoc report is recovered while the
original outer parser failure remains retained. No full regression, current phase/compact/A/EME
or cable acceptance, feature completion or release certification is claimed.
The preceding v8 matrix and cable/native-GUI passes below are historical and
do not qualify this changed candidate.

The focused matched 15 kW baseline runs in `local-evidence/helper-v9/` are
**incomplete**, not passes. Shared-crystal Rate 36 produced no bits in either
20-second window (45.79 seconds of capture each). Independent crystal/ic-7100
Rate 360 with Fake-FCC produced no bits in either 55-second window; helper ON
advanced 2.05 seconds of capture. The separate GPSDO-OCXO 128-second EME case
produced no bits in either 20-second window; helper ON advanced 0.341 seconds.
None executed conditional acquisition work. `baseline-binding-r1.json` binds
the preserved baseline ELF, archives, metadata-fixture repair and reaped runs.
The original incorrect deferred-FHSS metadata expectation remains a retained
fixture failure; its repaired metadata-only check passed without a DSP change.

The coarse frontend's initial standalone compile and focused tests passed
(3.87 seconds and 0.42 seconds; no warnings, processes reaped), bound in
`frontend-focused-v1/execution-result.json`. This proves the component's
discovery/translation controls, not normal rolling reception or payloads.
Compact focused checks have demonstrated matched 12-bit physical completion,
including final-symbol helper loss, with fewer executed sample projections and
lane accumulations. The original trial-matching, calendar and pressure failures
remain retained alongside their individually reviewed repairs below. These core
checks do not establish normal rolling reception. The separate physical-capture
build's missing `/usr/bin/time` setup error is also retained; no compiler ran in
that initial attempt.
Its corrected timing wrapper then compiled, linked and passed the physical
capture test (0.064 seconds), including raw +3/+6 kHz support, default narrow
rejections, early helper energy, unchanged source drain and payload coordinates.
The phase-dynamics component also passed (0.032 seconds), with an actual
Receiver/Generator check that metadata-forwarded phase cannot become a fresh
observation; `dynamics-focused-v1/execution-result.json` binds that check.
All component processes were reaped. These passes do not establish Live
integration, a current GUI build or any of the three end-to-end acceptance cases.

The first coherent application build passed (145.25 seconds, exit 0/reaped,
unchanged input hashes), in `production-build-v1/`. Its focused component and
history/physical-tail group passed 5/5, and the shared GUI helper check passed.
Planner focus failed before selector comparison: the Rate 360 deferred-FHSS
profile's real gate-skirt allowance crosses audio DC, which the symmetric
helper placement incorrectly rejects. The independent selector retained all
seven LF lanes. `planner-diagnostic-v1/` retains that distinction; a typed
real-mono placement correction is not yet built or qualified.

The separately bound compact core in `compact-coherent-v2/` passed clock-cell
coverage, strict trial parity, periodic aliases, audit/reclaim and matched
short-payload controls. Correcting the calendar fixture to use the existing
physical absence-capture duration produced exact eight-bit/END and competing
start passes (108 to 12 and 810 to 90 acquisition jobs). The original calendar
test supplied insufficient eligible absence and remains a retained failure.
Two memory fixtures first failed in the ordinary helper-OFF control, under
loans below its required future workspace; these were invalid resource setups,
not demonstrated helper regressions. The reviewed long-symbol repair uses the
same ordinary 82,464-byte promise for both sides and passes complete two-bit
128-second reception, final-symbol helper loss, progress/identity and END.
The first revised pressure pair completed exactly under the same measured
66,855-byte loan but failed to observe actual subsequent helper-control
reclamation. A reviewed, test-only pressure-frontier repair then passed the
original reclamation predicate: the optional control was present after actual
cold-work omission and yielded at sample 2,712. Both controls retained exact
12-bit data, per-poll progress, pending identity and physical END under that same
loan. Executed carrier projections changed from 286,929 to 265,233, and lane
samples from 2,810,583 to 2,548,423. The original failed run remains distinct;
`compact-coherent-v2/reviewed-r3/result.json` binds the repaired pass.

Actual raw and 128-second segmented phase controls pass exact data and END
through reference closure. In the controlled nonzero-phase long case, the
uncorrected receiver does not recover the exact two bits, while the corrected
receiver does; first-symbol evidence changes from 1,396.24 to 158,571. This is
an isolated phase-kernel comparison, not measured normal Auto acquisition or a
CPU benefit. `compact-coherent-v2/reviewed-r2/result.json` binds these results.
The current post-build Live repairs and normal phase caller are not covered by
the first application build or these component checks.

The second coherent GUI/application build passed in 127.48 seconds with unchanged
build-input hashes (`production-build-v2/`). Its focused group passed 10/11;
the new calendar-bandwidth test incorrectly requested isolated Low at 15 kW
with too little guard space. The reviewed test-only correction moves that
synthetic identity check's carrier from 1 to 4 kHz, preserving power, all three
placements and the actual HF/EME geometry. A private compile/link against the
unchanged second-build library passed the repaired test (0.008 seconds), bound
in `calendar-repaired-r1/result.json`. The original failed fixture and 10/11
result remain retained. Shared GUI helper, Planner helper and
all three 15 kW acceptance metadata checks passed separately.

Actual encrypted Auto/rolling diagnostics of this second build remain
**incomplete**. Shared-crystal HF recovered the exact first five of eight bits
in 45 seconds, with a qualified preconversion reference and both signed input
views; 20,524 executed native jobs all belonged to conditional views, including
the primary wire and later epochs. There was no reset or overrun, but the run
did not cover the full 114-second payload or physical END. Independent
crystal/ic-7100 HF initially selected 21 of 2,075 pairs and executed 2,688
conditional jobs, then a newer nonoverlapping frequency interval triggered
quarantine and broad acquisition. Its 60-second window advanced only 2.81
seconds of audio and received no bits. The trace proves this rejection branch,
but lacks the rejected measurement needed to quantify its cause. Local
frequency covariance omits the configured oscillator phase diffusion; that
model/provenance gap is being corrected, not replaced by an empirical gate
increase. `stage2-hf-crystal-on/` and `stage2-hf-fhss-on/` bind sealed traces,
commands, binary hashes and reaped processes. Neither is a full acceptance or
before/after performance pass.

The actual 15 kW weak-helper observer exposed a separate frequency-confidence
defect. At 205 dB path loss, one of 264 published intervals missed the simulated
carrier; at 210 dB, 111 of 363 missed it. The first reported interval below
0.1 Hz at 210 dB was wrong by 0.8703 Hz despite a 0.0250 Hz half-width. Eighty-six
invalid intervals lay wholly inside the declared oscillator domain, so a domain
or adjacent-interval overlap gate alone is insufficient. Private provenance
diagnostics reproduced both raw local-fit misses and long-fit amplification;
the narrow uncertainty is not accepted as justified assistance. These are
helper-only observation checks, not ordinary payload or Live passes. The four
60-second signal/noise streams, unchanged inputs, reaped processes and failed
coverage remain in `eme-precision/` and `weak-frequency-diagnostic-v1/`.

The replacement strong-tone process allowance passed a separate private focused
check under the original 9 KiB receiver bound (2,664-byte measured stack chain
within its 2,880-byte scratch allowance). With matched oscillator phase noise,
the original local-only calculation missed the mean carrier in 11/56 references
and rejected the initial link interval once; the conservative pathwise model
missed none of the same 56 references and caused no such rejection. Its largest
frequency process half-width was 0.9466 Hz. All 47 independently dated fitted
phase measurements also covered their actual endpoints (maximum error 0.00134
radians, maximum total allowance 0.0791 radians). A 4 Hz/s chirp produced no
qualified reference; a retained 0.2 Hz/s chirp observed for 12 seconds produced
36 fixed-interval rejections. Earlier four-second chirp fixtures legitimately
failed to leave the wider interval and remain retained test-setup failures.
These are conditional CW/AWGN/oscillator-model checks, not universal interference
confidence, full Live validation or a repair of the weak global-frequency path.

The third coherent application build passed in 52.13 seconds with unchanged
inputs (`production-build-v3/`). Its focused CTest group passed 6/6, including
the causal frequency-support rebase, oscillator dynamics, calendar geometry and
both payload-tail controls. Shared GUI helper, Planner helper and the three
acceptance metadata checks also passed (`frequency-focused-v3/`). These are
focused results; full current-source qualification remains pending.

The same 60-second encrypted Rate 360 Auto/rolling diagnostic advanced 9.81
seconds of audio with the reference continuously qualified, compared with 2.81
seconds before the process-model correction. It still failed to complete:
one pending bit, no physical END, 269,528 acquisition jobs and 382.59 process
CPU seconds. Twelve empty epoch candidates exhausted their payload-renewed
probation and replayed exhaustive windows, accounting for 199,200 of those
jobs. Every affected trace reported `audit_due=0`; this is a candidate-lifetime
defect rather than a scheduled broad audit. `stage3-hf-fhss-on/` retains the
incomplete result and sealed traces. The correction must keep a continuously
qualified tone as the conditional link reference and use separately owned,
fully scored future broad audits; missing payload alone cannot invalidate it.

The fifth focused application build and six focused CTests passed, including
the repaired helper-loss-during-audit lifecycle control; GUI, Planner and three
metadata checks also passed (`frequency-focused-v5/`). The fourth run's failed
Simulation counter oracle and subsequent audit-generation overlap defect remain
preserved in `frequency-focused-v4/`. These focused passes do not qualify the
acceptance payloads or the final source.

The next unchanged 60-second Rate 360 diagnostic still failed: one matching
pending bit, no END, 8.39 seconds of audio and 478.12 process CPU seconds
(`stage5-hf-fhss-on/`). That bit belonged to candidate epoch 1800000004 while
TX used 1800000005; it was not true-epoch acquisition. Joined work comprised
58,128 conditional and 315,400
future-broad template-descriptor visits. Template visits include exact reuse;
they are not a count of FFT executions. One periodic audit admitted 16 complete
broad banks, so the future-audit policy still overwhelmed the conditional path.
These separate helper-on runs resolved percentage workspace independently;
their CPU differences are not controlled before/after measurements. The later
focused builds exercise a revised partial future-probe policy and explicit
backend telemetry without establishing an acceptance-case pass.

The sixth coherent application build passed in 76.90 seconds and its focused
targets built in 29.34 seconds (`frequency-focused-v6/`). Eight of nine receiver
checks passed, including normal helper loss, phase continuity and both tail
controls; GUI, Planner and acceptance geometry also passed. The lifecycle check
remains incomplete: its fresh broad fallback opened, but capture stopped 2,480
frames before the newest candidate's first complete scoring fence. Its retained
SF128 waveform captured 9.55 seconds during the ten-second wall window; the
required fence was about 9.86 input seconds after loss. This does not establish
a receiver retirement defect or a categorical ten-input-second impossibility.
A reviewed SF64 functional fixture retains every assertion and wall limit;
its subsequent v7 rerun passed, while the original result remains unpassed.

Three of four focused CLI helper checks passed. Clear-time metadata failed;
the same WAV decoded all twelve Robust bits and completed physically. An
identical-PCM receiver comparison produced 51 metadata updates without declared
diffusion and none with the crystal model: its conservative process uncertainty
exceeded metadata's local coherence gate. `cli-metadata-model-v2/` retains the
WAV and both streams. The subsequent metadata-only local-fit gate correction
passed the v7 CLI checks below; published synchronization bounds and acquisition
remain separate from that metadata decoding gate.

The seventh coherent application build passed (139.31 seconds), its focused
targets built successfully, and all ten component/history/physical-tail CTests
passed. Shared GUI helper, Planner helper, all four CLI helper checks and the
three acceptance metadata geometries also passed. Processes were reaped without
timeouts or changed inputs. `frequency-focused-v7/execution-result.json`,
`inputs.json` and `production-binaries.json` bind these focused results: Live
`605e6061`, its helper `76cf005a`, Live test `a0a4ba24`, core archive `d6a36171`
and GUI `ae3d683c`. This resolves the focused lifecycle/metadata fixtures, not
the acceptance payloads or full final-source qualification.

The subsequent stage 7B encrypted Rate 360 Auto/rolling diagnostic failed its
gate under a later-diagnosed TX fixture mismatch: zero primary bits and one
secondary row, with TX epoch 1800000006. It
advanced 198,579 samples at 14,400 Hz (13.7902 seconds of audio) during
60.068934 wall seconds and 153.010143 process CPU seconds. Joined work was
115,848 template-descriptor visits: 99,248 conditional and 16,600 audit.
Separate arithmetic counts were 53,875 FFT jobs, 29,648 direct jobs, 32,325
exact-reused template visits and 215,500 tabulated FP64 transforms; these units
are not summed into one arithmetic cost. `stage7-diagnostics/execution/` retains
the result and wrapper log, sealed complete trace, unchanged source/binary
bindings and reaped processes. Its failure kept the 1,024-bit long pair gated
and unrun. The 6,083,008,512-byte percentage-resolved workspace differs from
earlier independent invocations, so this is neither a controlled causal
regression comparison with stage 5 nor a performance gain. The omitted TX FHSS
plan described below also prevents treating it as a matched GUI configuration
or evidence of core payload truncation.

The first private global weak-frequency replacement retained every compatible
frequency/mask cell and had no interval misses in its six controlled streams.
It did not meet the original 24 dB-Hz or wide shifted-tone utility checks:
the minimum interval half-widths were 6.89 and 95.08 Hz, respectively. The
205 dB EME observer first reached 0.1 Hz after 6.4 seconds; at 210 dB its best
half-width was 0.666 Hz. Noise-only input published no reference. These are
helper-only prototype results in `global-line-profile-v1/`, not accepted
production assistance or payload evidence. The alias/frequency-confidence
correction and its original utility checks remain open.

The reviewed fourth private replacement adds a constant-amplitude residual
within each clean dwell and retains the full frequency/mask union. Its prototype
24 dB-Hz stream reached a half-width at most 0.1 Hz at 7.20 seconds. All
15 intervals covered the carrier; evaluation used 7.81 process CPU seconds for
12 seconds of audio. Precision was intermittent. The stronger and shifted controls
passed, as did 16-second EME controls over the actual ±1.7400009 Hz prior:
205/210 dB loss each published 25 intervals with no misses, first at 6.40 seconds,
minimum half-widths 0.0293/0.0500 Hz, and 0.0545/0.0645 CPU seconds per audio
second. Noise published no reference. Actual object storage is 139,480 bytes
and measured evaluation stack 2,032 bytes. Original failed prototypes remain in
`global-{line,ray}-profile-v*/`. These are conditional coherent-tone/AWGN/model
component results, not production, long-history, payload or Live evidence.

The subsequent optional-work API passed its lifecycle, resource and unchanged
strong-rate controls, but failed the literal maintained 24 dB-Hz utility stream:
15 intervals over 12 seconds of audio, none at most 0.1 Hz by the unchanged
ten-second acquisition deadline. The earlier prototype differs from that stream
by a constant 0.08646-radian phase; its pass does not qualify the original
fixture. The stronger and shifted-tone utility checks passed separately.
`optional-frequency-api-v2/original-utility/` preserves these results. A bounded
diagnostic reproduced the failure and retained all compatible frequency islands;
the best complete union had a 0.101318359375 Hz half-width. No island was selected
or clipped to obtain a narrower result. That private API result remains a
retained utility failure, separate from the later V5 literal-stream result below.
The subsequent paired-projection candidate also failed the same literal
fixture (18.40 seconds, fifteen covering intervals, none meeting the precision
deadline). Its observational trace shows surviving aliases using time masks
that cross punctuation and whose own best fitted frequency is wrong. All
compatible islands remain retained. This is a diagnosed estimator limitation,
not permission to select a preferred island or relax the original utility test.

The separate mean-frequency/affine-support phase query component passed all
five compile/link/positive/unsupported-arithmetic commands, with unchanged
inputs and reaped processes, in
`phase-mean-interval-private/execution-v1/result.json`. Header `3f27ba3b` and
test `b928e355` were subsequently integrated. At that stage the runtime caller
was unchanged; this component result is not a normal workload or work-reduction
proof.

The next coherent build, `frequency-focused-v8r1/`, passed in 52.00 seconds;
all 13 focused CTests, shared GUI helper, Planner helper, four CLI helper checks
and three metadata geometries passed with unchanged inputs and reaped processes.
The initial v8 attempt remains a configuration failure: the new native timing
CTest lacked its declared build prerequisites. The corrected receipt,
`execution-result.json`, `inputs.json` and `production-binaries.json` bind
Live `82df4aa6`, its helper `69854335`, phase header `3f27ba3b`, core archive
`a3e4892d` and GUI `6d4035ad`. These are focused current-reference, independent
frequency/phase, timing and ordinary completion checks, not normal acceptance
or final-source qualification.

The separate conservative UTC RF mapping and finite-aperture mean-caller guards
passed both compiles and both regression commands, with unchanged inputs and
empty process groups (`combined-utc-mean-private/execution-v1/result.json`).
The UTC check included 1.58297 Hz extra RF uncertainty, all seven original LF
rates and nine joint boundary points, without a shared-radio correction. The
mean-caller check reduced a controlled 0.3 Hz raw radius to 0.00170045 Hz over
samples 10,000–28,000, retained eight direct fallbacks and granted no future
precision lease. Tested candidate sources were `a36de520`/`f4fab968`/`79dd55ab`,
with guard wrappers `29fec28b` and `b01f3337`; these are component guard results,
not measured normal acquisition or performance.

The RF-truth diagnostic also passed compile, link and its fixed-epoch run in
`true-epoch-b-v1/rf-truth-all-clocks-execution-v1/result.json`, with unchanged
inputs and reaped processes. It recovered exact 12-bit data with ordinary
physical END using DATA CFO truth 30.15 Hz and all seven original LF lanes.
Wrapper `5c57fa64` and ELF `a5fa480b` bind this diagnostic against core
`a3e4892d`. Supplying RF truth and a fixed epoch does not establish helper
measurement accuracy, unknown-epoch Auto acquisition or a normal acceptance
pass.

The private V5 aperture estimator passed the literal maintained 24 dB-Hz stream
in `optional-frequency-api-v5-aperture/execution/legacy24-result.json` (6.03
wall seconds, reaped without timeout). All 11 published intervals were usable;
the first was at 7.99998 audio seconds, maximum carrier error 0.00207031 Hz and
maximum interval half-width 0.0507813 Hz. Matching before/after bindings retain
receiver `b9fba408`, API `ed9a44f3` and fixture `d127cb8d`, linked to retained
core `99cc4718`. This component pass does not qualify production weak Live,
EME, other streams or the earlier failed API versions.

The combined caller/publisher application build and isolated `datapump` target
first failed the unchanged auxiliary assertion `16496 <= 16384`, 112 bytes over
budget. Both reaped failures remain in
`combined-utc-mean-private/coherent-compile-failure.log`. A four-entry compact
numeric history then restored the existing 16 KiB bound. The coherent v9
application and focused targets built in 52.51/31.11 seconds. Eleven of twelve
focused CTests passed; `helper_receiver` still failed its line-335 usable/early
reference assertion for the 30.02 dB-Hz case, whose weak-profile funding is not
merged. GUI helper, Planner helper and nine metadata configurations passed.
`frequency-focused-v9/{execution-result,inputs,production-binaries}.json` binds
GUI `26ceb229` and archive `46953f31`. Ten later query/prepayment/diagnostic
postimages are separately pinned as unbuilt in
`normal-gpsdo-same-pcm-private/source-overlay.json`; the v9 runtime and old ABI
headers used by private probes do not qualify them. Those changes and the
promoted fixture/getter were subsequently rebuilt in v10. Its initial
timing-wrapper attempt exited 127 because `/usr/bin/time` was unavailable,
before a build ran.

The coherent v10 application build passed in 142.255 wall/279.656907 process
CPU seconds, with GUI `51b7e7d7` and core archive `28e6c757`. The first focused
compile failed for a missing direct `fhss_schedule` include; its first header
repair then exposed a missing `symbol_schedule` resolve-function declaration.
Both failed attempts remain retained in `integration-v10/`. Adding only those
two direct headers produced a passing focused compile in 2.1684 wall/4.26426 CPU
seconds and all seven CTests passed in 42.7956 wall/17.45655 CPU seconds:
retained growth, native work bound, FFT-probe prepayment, helper Live, phase
continuity and both payload-tail controls. Source guards were unchanged during
each run and processes were reaped. `schedule-header-repair/execution-result.json`
binds that focused result; no assertions or deadlines changed. This is not full
normal, cable or acceptance qualification.

All three original `normal-b12-triplet-v9/` legs failed or remained incomplete.
A causal fixture diagnostic found that TX omitted the selected FHSS plan while
RX expected FHSS: Session's default empty plan clears TX hopping, whereas the
GUI Controller passes its selected plan. Stage 7B used the same omission.
The original failed results, traces and actual work counts remain retained as
fixture-setup evidence, without a core-truncation or valid matched-configuration
claim. Corrected fixture `1a0bcbaf`, which passes the selected GUI TX plan, has
now been promoted and rebuilt in v10; the private results below retain their
original v9 runtime bindings.

The corrected private B12 triplet completed with an incomplete overall result,
bound by `normal-fixture-matched-tx-private/execution-b12-triplet-v9/` receipt
`bd347eb8`. OFF recovered 0/12 bits; ON had nine contiguous true-TX bits after
12.6808 audio seconds, while the GPSDO control strictly passed 12/12, stable
pending identity and ordinary physical END after 18.7633 audio seconds. ON's
capture deadline preceded the final symbol end, so its incompleteness does not
demonstrate truncation or false END. The selected primary commit context was
epoch 1800000005 rather than actual TX 1800000006; true-TX nine-bit continuity
is separate trace evidence, not a completed primary acquisition. ON used
195.324629 process CPU seconds versus GPSDO's 6.886595 with unequal processed
media, so no normalized gain is claimed. All launched processes were reaped;
the old wrong-plan failures remain retained.

The corrected private same-PCM helper-OFF GPSDO control passed rolling reception
of the exact 12-bit TX wire `101011100000`, source, stable pending row and
ordinary physical END. Its 18.7633 audio seconds used 6.5359 process CPU seconds
and 3.7898 wall seconds, with 10,975 FFT jobs across 34 owners; this is not a
helper gain comparison. `normal-gpsdo-same-pcm-private/execution-same-pcm-fhss-v1/`
binds receipt `3f9238a0`, private ELF `f4f5eed1` and PCM `6e11efe4` against
archive `46953f31`. A separate fixed-epoch EOF replay is diagnostic only.
Earlier probe failures `491e6ae4`/`0a1061c7` remain preserved; the r1 complete capture
proved that actual TX wire differed at bit 8, rather than proving lost capture
or a truncated Robust decoder.

The private retained-growth getter reproduced the 128-state closure failure.
Its R2 160-state arrays passed focused accounting/source-gate checks in
`retained-growth-private/r2/focused-r2-v1/` (receipt `7c4910a1`), bound to source
`436a604c` and patch `b3d2206f`. R2 has now been promoted and rebuilt in v10,
whose retained-growth CTest passed. Its existing focused evidence remains
source-bound, not a maximum
grown-payload or transient heap-peak witness, Live funding or END qualification.

The joint-clock component retains three distinct earlier failures in
`native-clock-v1/`: R2 (`3bbe9c23`) failed to compile because one unsupported-drift
call lacked the new clock parameter; R3 (`08a12d0d`) corrected that call
(`1bd588fe`) but its
all-ones query mask was unsupported test setup. R4 used the production legacy
helper mask and passed query geometry, then its payload setup (`c031f02b`) failed because a
scalar helper-placement overload used the wrong gate/passband geometry. R5
changed only that fixture to the exact production placement; no earlier failed
result is replaced.

R5 passed actual encrypted 12-bit reception for both `c = +/-0.0003` cases,
using 14 actual punctuation markers, helper 5,275 Hz and measured CFO interval
[0.52749, 0.52751] Hz. Both controls retained the same pending row, per-bit
progress, full final wire and ordinary END at sample 230,407, including helper
loss near the final symbol. FFT jobs changed from 8,025 to 6,885 (14.21% fewer);
process CPU changed from 8.89568 to 7.86672 seconds and from 8.92711 to 7.88196
seconds, about 11.6% less in this isolated comparison. This is a fixed-epoch
component result, not a normal Auto acquisition gain.
`native-clock-v1/execution-r5/result.json` (`4667b9c0`) binds packet `b630b81a`
and payload ELF `d89834bf`; the actual output is
`test-actual-encrypted-payload.log`. Inputs were unchanged and processes reaped.
The core, headers and maintained payload/query regressions were then integrated.

The coherent v11 application build passed in 140.3982 wall/276.492169 process
CPU seconds; focused compilation passed in 14.77096/27.81078 seconds and all six
CTests passed in 34.4972/34.485186 seconds. These cover retained growth, frontend,
native work bound, FFT-probe prepayment and joint-clock payload/query components.
`integration-v11/{execution-result,inputs,production-binaries}.json` binds GUI
`1bf1af11`, core archive `08a4d2db`, receiver `1bd588fe` and unchanged run inputs;
all processes were reaped. The pure engineering seed query was promoted and its
frontend check passed. Later cold-eligibility API/test changes were promoted
after v11 and built in v12 below; no full normal/cable result is claimed.

The preceding actual 48,960 Hz coarse/optional-frequency pipeline component
passed its signal and noise commands in
`optional-frequency-api-v6-native48960/`. The controlled 12-second signal had
49.7403 dB-Hz C/N0, +3 kHz CFO, 100 ppm audio error, encrypted metadata enabled,
no Robust payload and no known RX timestamp. It published 26 truth-covering
references, first at 1.5613 audio seconds, with interval half-widths about
0.155–0.161 Hz; the coarse seed itself was not treated as confidence or signed
RF authority. Noise produced no seed or reference. This is a strong-signal
pipeline component pass, not normal A acquisition, a <=0.1 Hz utility pass,
hardware reception or general false-alarm qualification.

Its resource receipt reports 216,720 optional bytes (208,528 heap plus 8,192
worker scratch), a 2,872-byte strong call chain within 2,880, and 9,078 base
bytes with full scratch. The coarse grant was separately 181,496 bytes. These
explicit component grants are not proof of donor availability in a normal Bank.
The resource-foundation donor-interface check also passed, bound by
`frontend-budget-private/execution-r2/`: its synthetic two-signed-view interface
reported 1,315,160 bytes against a 2,518,520-byte ceiling. It explicitly provides
no Bank, payload-completion or normal A funding claim.

The coherent v12 application build passed in 56.062947 wall/76.565216 process
CPU seconds; focused compilation passed in 22.582919/45.001243 seconds. The
74.561245 wall/52.115457 CPU runtime group passed 16/17. `helper_live` failed
after 24.38 seconds with missing broad-work, periodic/fallback-admission and
post-loss-coverage observations. Its complete 522-record trace had no drops
and is retained at `/tmp/datapump-helper-lifecycle-237024062547547.jsonl`.
That original lifecycle failure remains retained; the narrower v13 result below
does not convert it into a pass.
`integration-v12/{execution-result,inputs,production-binaries}.json` binds GUI
`dfc3e6d4`, core archive `2a00f0c2`, native optional API `1cf626e8`/`4a22ee2e`
and Live `a4ae1b55`. Raw-A mean, resource foundation and cold-stage component
changes are built; the new weak/staging/joint Live consumers remain private.
All recorded inputs were unchanged during these runs and processes reaped.
This focused result does not qualify full normal/cable reception or overall
acceptance.

The retained v13 Live lifecycle failure is bound in `integration-v13/`: focused
compilation passed, but only the raw-clock-bridge test passed at runtime.
`helper_live` still failed after 27.53 seconds, with `partial_slot=0` and
`one_cold_owner=0`; broad work, full coverage, periodic admissions and post-loss
coverage were observed. The reviewed boundary is a one-second reclaim before
the 1.28-second scoring fence, rather than the earlier reset-loop hypothesis.
Its complete 510-record trace is retained at
`/tmp/datapump-helper-lifecycle-237941270195306.jsonl`. The failed coverage
predicates remain required, without a deadline or assertion waiver.

The actual Rate 360 Auto/rolling FHSS B12 comparison remains **incomplete**,
with matched 2 GiB workspace and 15 kW helper when ON. The
[v13 baseline receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v13-baseline-v1/execution-result.json)
(`a3bc7b3e`, archive `a8d1e0a3`, fixture `b58c0f8b`) reported helper ON 9/12
pending, OFF 0/12 and GPSDO strict 12/12 with END. The
[v19 AFTER receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-after-v1/execution-result.json)
(`bc23b3aa`) reported ON 7/12 pending without END, OFF 0/12 and GPSDO strict
12/12 with END. The AFTER source observer proves that TX had generated only
10/12 bits and neither TX nor channel had finished. This is throughput-incomplete
capture, not evidence of lost final bits or decoder truncation; baseline TX
source position was unavailable and is not retroactively inferred.

The [observational reporting recovery](../local-evidence/helper-v9/helper-fft-cache-private/normal-after-v1/observational-reporting-recovery.json)
(`9f026d64`) preserves the failed receipt and reconstructs only an interleaved
trailing trace-path field, without changing numeric fields or runtime. Across
the same committed raw-coordinate prefix 0–173,017, measured process CPU bounds
changed from 139.783–139.829 to 70.139–70.177 seconds, while wall time increased
from 45.436 to 55.789 seconds. The prefix includes helper lead, and retained PCM
was unavailable for bitwise waveform comparison. Neither this bounded CPU
observation nor the unequal whole-leg media establishes payload throughput,
hardware backlog or completed-message gain. Inputs and original failed results
remain preserved and launched processes were reaped.

The [corrected true-owner summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-after-v1/observational-true-owner-summary-r1.json)
(`c9c21dd6`) supersedes the marker interpretation in retained summary `a031ba93`:
0/256 clock attempts qualified, but 222 stopped at `unavailable_scored_support`
(reason 4) and 34 at `invalid_snapshot` (reason 2), before marker qualification.
The default marker text did not establish absent punctuation: 169 references
had known punctuation spanning 29 distinct raw marker samples. The accepted
captured-lookahead support repair
[clock-ingress R2](../local-evidence/helper-v9/clock-ingress-v1/clock-ingress-r2.patch)
(`bb801f3d`; rejected duplicate-accessor R1 retained) is integrated in v20 Live
`0159027b`. The subsequent
[normal v20 ON receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v20-on-v1/execution-result.json)
(`201e6c29`) remained incomplete: RX 7/12 pending without END, TX only 10/12
generated and unfinished, 75.173846 process CPU/60.084448 wall seconds.
Its [observational summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v20-on-v1/observational-summary.json)
(`e69fb903`) shows that the support repair reached the clock qualifier in
206/256 setup attempts. Of those, 203 were unqualified (161 markers did not
enclose the scored support; 42 awaited distinct markers); three supported fits
declined the original joint-cell certificate. No qualified narrowing executed
for a true-TX owner. This proves qualifier reach, not clock work gain, full
reception or a completed-message improvement. The failed result is retained
and all launched processes were reaped.

The separately read FFT-cache component receipt
`helper-fft-cache-private/focused-v13-v1/execution-result.json` (`8b9300ec`)
passed arithmetic, exact R5 payload/row/progress/physical-END/loss and matched
helper-OFF layout/quota controls. It retained worker slots and kept the actual
cache within its cap. This is private component coverage, not normal rolling
performance or a repair of the lifecycle hold.

The subsequent v14 focused build failed in
`conditioned_reference_factory.cpp`, where `deferred_symbols`,
`scheduled_bursts` and `scheduled_symbol_offset` were undeclared.
`integration-v14/execution-result.json` retains the failed compile (112.7252
wall/223.09058 process CPU seconds), unchanged inputs and reaped execution.
No v14 runtime, application/GUI rebuild or full coverage is claimed.

The v15 focused build passed in 96.63379 wall/191.69458 process CPU seconds.
Its runtime group passed 9/13 in 9.89583 wall/9.86925 CPU seconds, bound by
`integration-v15/execution-result.json` and `focused-runtime.log`. The four
original failure strings remain retained:

| Test | Recorded failure |
| --- | --- |
| `conditioned_first_bit` | `Native source prefix does not cover the observed left FIR halo` |
| `conditioned_reference_factory` | `factory fixture lost user's production B sampled/calendar geometry` |
| `helper_estimate_scenario` | `helper masked scenario widens an original physical cell` |
| `helper_native_work_curve` | `Actual native work descriptor is currently bounded to the B Interleave10 deferred-FHSS waveform` |

The clock bridge, cold configured-workspace floor, Planner forecast, pure
capture-transport construction, strong/literal-24 helper cases, presence
prepass work and native signal/noise pipeline components passed. This does
not qualify transport-gap execution, normal reception or the private weak-stage
and clock consumers. All launched commands were reaped. There is no v15 GUI
update, full coverage or feature-completion claim. Later outcomes below do not
rewrite this failed run.

The [v17](../local-evidence/helper-v9/integration-v17/execution-result.json)
forecast-fixture `gap` name conflict and
[v18](../local-evidence/helper-v9/integration-v18/execution-result.json)
incompatible `HelperWorkspace` presence-field resets remain retained compile
failures. The [v19 result](../local-evidence/helper-v9/integration-v19/execution-result.json)
rebuilt the application but failed its lifecycle and work-curve runtime checks.
None of those runs is promoted to a full pass by the later fixes.

The [v20 receipt](../local-evidence/helper-v9/integration-v20/execution-result.json)
records a passing focused build (168.1345 seconds), runtime 8/10 (29.7180
seconds) and application rebuild (8.6878 seconds), with unchanged inputs and
reaped commands. Its
[binary bindings](../local-evidence/helper-v9/integration-v20/production-binaries.json)
pin GUI `9ce083a5` and archive `e2398045`; the
[input manifest](../local-evidence/helper-v9/integration-v20/inputs.json) pins
Live source `0159027b`. Cold input stage/workspace, clock bridge, first-bit, factory, scenario,
forecast and capture components passed. The retained v20 failures were:

| Test | Failed coverage in v20 |
| --- | --- |
| `helper_live` | Only `partial_slot=0`; `one_cold_owner=1`, `workspace_observed=1`, two periodic admissions and all other fixture flags were observed. |
| `helper_native_work_curve` | Native work geometry is available, but sampled first-bit Monte Carlo declines: `Production template evaluation exceeds bounded reference budget`. |

The work-curve run now uses the actual ordinary sampled UTC catalogue: 2,075
pairs and 600.09 ppm clock half-domain, rather than the unsteered 777/300 ppm
geometry. This correction does not itself predict CPU, SNR, probability or
completed reception. The accepted weak/startup and raw-clock integrations are
built/source-reviewed; full normal startup, payload, phase, compact, A, EME
and current cable qualification remain open. Full current-source regression,
sanitizer and release qualification have not passed.

The [v21 receipt](../local-evidence/helper-v9/integration-v21/execution-result.json)
records passing focused compilation and application rebuild, then 9/10 runtime
passes. `helper_live` now passed its unchanged lifecycle predicates; only
`helper_native_work_curve` failed the finite production reference budget.
[Binary bindings](../local-evidence/helper-v9/integration-v21/production-binaries.json)
pin GUI `ad7d16da` and archive `b296c060`. These focused passes retain the
earlier failed runs and do not establish full acceptance.

The [v22 receipt](../local-evidence/helper-v9/integration-v22/execution-result.json)
records passing focused/application builds and 10/14 runtime passes, with
unchanged inputs and reaped commands. Its
[binary bindings](../local-evidence/helper-v9/integration-v22/production-binaries.json)
pin GUI `c46ff965` and archive `9f7aacfd`. Selector-cache focus passed in 2.24
seconds: exact evidence, trial/descriptor coverage and next-event controls
matched its uncached control, with actual tabulated FP64 transforms reduced.
The logged FFT-pair-job counts were 1,698 versus 1,992; these are distinct from
transform counts. Numeric FFT batch focus passed in 4.43 seconds and helper
Live lifecycle in 42.49 seconds. The earlier work-curve budget check now passed
in 1.65 seconds with finite-reference template reuse; that does not qualify all
first-bit/Monte Carlo cases or normal reception. The four retained failures are:

| Test | Recorded v22 failure and subsequent focused result |
| --- | --- |
| `conditioned_first_bit` | `Calculation cancelled`; the negative test caught the wrong exception class. Repaired focus passed in v23. |
| `conditioned_first_bit_monte_carlo` | The same negative-test exception mismatch failed v22; repaired focus passed in v23. |
| `helper_planner_forecast` | `UI omitted separate conditional curves or hid their actual limits`; repaired focus passed in v23. |
| `helper_joint_cost` | `joint cost trusted original full-catalogue builder declined`; the later v23 failure and v24 test-only correction are recorded below. |

These component passes are not full-candidate, normal-HF, performance or release
qualification; the original v22 failed receipts remain retained.

The [v23 receipt](../local-evidence/helper-v9/integration-v23/execution-result.json)
records a passing focused build (99.6496 wall/198.2398 process CPU seconds),
application rebuild and 12/14 runtime passes (82.0970 wall/59.1969 CPU seconds).
The Monte Carlo focus passed in 30.49 seconds with finite-reference
reuse. Two fixture failures remained: `live_helper_clock_bridge` used a derived
clock factor of −300.075 ppm outside its declared ±300 ppm domain, and
`helper_joint_cost` treated payload-link acceptance as the predicate for actual
numeric execution. Their original failed logs remain retained.

The [v24 receipt](../local-evidence/helper-v9/integration-v24/execution-result.json)
records only corrections to those two test oracles. Focused compilation passed
in 5.6287 wall/10.1606 CPU seconds; bridge and joint-cost checks passed in
0.54 and 0.40 seconds (0.9659 wall/0.9627 CPU seconds combined), and the
application build passed in 1.7176 wall seconds. Its
[binary bindings](../local-evidence/helper-v9/integration-v24/production-binaries.json)
retain v23 GUI `50d7f079`, core archive `00dfac18` and normal-acquisition fixture
`19b5a9d8`. Together with the 12 unchanged v23 passes, all 14 focused checks
are resolved; this does not constitute full normal or performance qualification.

The [v25 receipt](../local-evidence/helper-v9/integration-v25/execution-result.json)
records passing configuration and a 15.53-second focused build of exact
continuation-row reuse. Its source bindings retain `pattern_receiver.cpp`
`559b7331` and row helper `cd8f9686`. Helper Live, joint-clock and selector-cache
checks passed in 41.37, 34.63 and 2.25 seconds. Both new continuation checks
failed an unsupported assertion that each input push could publish exactly
one new bit, despite exact fit/payload comparisons. These failures remain
retained; several accepted bits may legitimately appear in one progress poll.

The accepted test-only repair compares the entire per-push `Progress` vector
between scalar and reused execution, retaining monotonic exact prefixes, full
pending bits and ordinary physical-absence completion. The
[v26 receipt](../local-evidence/helper-v9/integration-v26/execution-result.json)
(`ba6ffa30`, [input bindings](../local-evidence/helper-v9/integration-v26/inputs.json)
`3f915c5e`) records both repaired checks passing in 84.43 and 68.11 seconds,
with unchanged runtime inputs and reaped commands. The production build passed
in 2.23 seconds; [binary bindings](../local-evidence/helper-v9/integration-v26/production-binaries.json)
pin GUI `79c2f036` and core archive `9b58a749`. Together with the three unchanged
v25 passes, these five focused checks are resolved, not a full normal matrix.

The component evidence includes 756 exact scalar row/score oracle comparisons
and a 64-bit full-receiver comparison with all 8,400 fits equal, exact full
pending payload, ordinary END and helper loss. That component reported process
CPU 42.3738 to 25.5118 seconds. It is not a measured normal-controller runtime
gain, current cable acceptance or feature completion.

The [v27 receipt](../local-evidence/helper-v9/integration-v27/execution-result.json)
(`ae2d4119`) records successful configuration followed by a compile failure:
the existing auxiliary-storage assertion evaluated `17520 <= 16384`. The
focused build exited 1 after 92.9938 wall/128.9630 process CPU seconds, with
owned processes reaped. No v27 runtime test or rebuilt GUI result follows;
v26 remains the last coherent runtime, and the resource failure is retained.

The [v28 receipt](../local-evidence/helper-v9/integration-v28/execution-result.json)
records passing focused and stack compilation before the explicit user
architecture pivot interrupted runtime work (parent interruption exit 130;
the receipt's runtime exit remains partial). Its
[runtime log](../local-evidence/helper-v9/integration-v28/focused-runtime.wrapper.log)
contains five passed tests and a failed `helper_live` lifecycle check, then
the start of `helper_joint_clock`. The 19-test selection did not complete;
neither all 19 passes nor a new-core runtime pass is claimed. The original v27
resource failure and v28 lifecycle failure remain retained.

The separate private raw-phase component has passing
[compile](../local-evidence/helper-v9/raw-phase-nco-qualification-r4-v26/execution/compile/execution-result.json)
and [link](../local-evidence/helper-v9/raw-phase-nco-qualification-r4-v26/execution/link/execution-result.json)
receipts. Its
[operator diagnostic](../local-evidence/helper-v9/raw-phase-nco-qualification-r4-v26/execution/functional-operator-diagnostic/execution-result.json)
passed, while the
[strong diagnostic](../local-evidence/helper-v9/raw-phase-nco-qualification-r4-v26/execution/functional-strong-diagnostic/execution-result.json)
failed `raw correction was backdated to observed evidence`; source diagnosis
found an invalid fixture-origin check. The causal test correction is
source-accepted but unrun, so that failed diagnostic remains failed.

Its [resource R1 report](../local-evidence/helper-v9/raw-phase-nco-qualification-r4-v26-resource-r1/artifacts/resource-result.json)
is held, not passed: unresolved/ambiguous callees leave project-owned stack
subtotals of 768 and 1,632 bytes incomplete, with external runtime stack
unproved. Measured retained class sizes are 104 bytes for the mixer and
1,192 bytes for control. No exceed was observed in this partial component
accounting; it does not prove a whole-chain resource bound, caller multiplicity
or current Live raw-phase qualification.

The [v21 B ON diagnostic](../local-evidence/helper-v9/helper-fft-cache-private/normal-v21-on-v1/execution-result.json)
(`74830527`) still reported 3/12 pending bits without END, with TX only 6/12
generated and unfinished, at 72.287311 process CPU/60.105498 wall seconds.
Its [observational summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v21-on-v1/observational-summary.json)
records 49 executed clock-conditioned batches/11,048 template jobs overall;
the true-TX owner accounted for two batches/456 jobs. Those batches selected
five of seven original LF rates with unresolved UTC retained. This is actual
conditional execution, not full-source completion or a matched performance gain.

The [v22 B ON receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v22-on-v1/execution-result.json)
(`054d1915`) remains incomplete: nine contiguous correct wire bits were pending,
without END, at 75.112198 process CPU/60.096969 wall seconds. There were no drops
or receiver resets; inputs were unchanged and all launched commands were reaped.
Its [observational summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v22-on-v1/observational-summary.json)
(`d7b4a940`) records all 12 generated wire bits `100000001101` and plaintext
bits `110101101110`, but only 100,915/148,300 TX source samples emitted and an
unfinished channel. The actual geometry was Fs 14,400 Hz, SF 64, H = 5,120
samples and T = 0.355556 seconds, rather than the earlier hypothetical
1.422-second symbol.

The shaped waveform ends at sample 235,848, beyond the captured 187,630. Only
185,582 samples had joined the receiver bank, leaving a final 2,048-sample
packet uncommitted. True-TX owner 13 remained alive without END; its next READY
fence at 185,920 was 338 samples beyond the joined frontier. Even the
six-second ordinary-absence lower bound is sample 256,240, beyond this capture.
This is an unfinished waveform/processing observation, not completion,
truncation or a throughput-gain result.

The same summary joins 49 executed clock-conditioned batches/11,320 jobs;
two true-TX-owner batches accounted for 464 jobs and selected five of seven
original LF rates. Cache accounting records 64 allocation lifetimes, 44
timing-priority endings and 20 still present at the final trace, with 18,470
fills, 73,938 hits and 36,940 forward transforms. Those are lifetime/event
counters, not simultaneous allocation, heap-peak, CPU or FFT-equivalent claims.

The subsequent [v23 B ON receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v23-on-v1/execution-result.json)
(`34b6a817`) and
[summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v23-on-v1/observational-summary.json)
(`9f44543b`) report all 12 wire bits exact and pending, without physical END,
at 64.039629 process CPU/60.167203 wall seconds. The last data endpoint was
sample 187,120; capture ended at 205,224, only 1.2572 seconds later. The ordinary
absence lower bound is sample 273,520. TX had emitted 118,068/148,300 source
samples, with waveform/channel drain unfinished. This remains an incomplete
single-leg result, not a passed 60-second gate, completion, truncation,
matched gain or real-time qualification.

The v23 summary joins 265 clock-conditioned batches/19,000 jobs overall;
15 true-TX-owner batches accounted for 1,072 jobs and selected one of seven
original LF rates. Its dated history has one allocation at sample zero and a
release at simulation join. All 30 observed constructor envelopes retained
clock history without grant reduction; the optional-cache trimming branch
was not exercised. Distinct numeric counts were 27,688 template descriptors,
14,035 FFT jobs and 40,350 tabulated FP64 transforms. These execution and
resource observations do not establish completed reception or throughput.

The explicit 120-second [v24 profiled B receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v24-on-profile-v1/execution-result.json)
(`8e53b244`) also failed full completion: all 12 wire bits were exact and
pending without END. TX and channel finished all 148,300 source samples, but
capture ended at 258,376, only 4.9483 seconds after the payload endpoint
187,120. The minimum ordinary-absence endpoint 273,520 still requires another
15,144 captured samples, followed by actual scoring. Its
[profile summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v24-on-profile-v1/observational-profile-summary.json)
(`ae874824`, trace `141b6a25`) records 124.778677 process CPU/120.228152 wall
seconds. This diagnostic does not replace the original 60-second failure or
establish payload completion.

Only the outer native-thread Context CPU total, 122.883312 seconds, is exact
for that profiling scope. Timer overruns left 75.077% of recorded expirations
unassigned; delivered stage samples cannot allocate that missing CPU. The
instrumented call counter recorded 308,520,820 template-build calls. Complete
committed input covers 17.8006 raw audio seconds including helper lead, with
about 6.98 process CPU seconds per raw second. Profiler overhead is
uncalibrated; these are not payload-throughput, real-time or hardware-backlog
measurements.

Source diagnosis found that established warm continuation still traverses the
2,075 original RF/LF pairs across seven LF rates and serially regenerates source
rows; the acquisition RF mask does not narrow that continuation work. The
subsequent v25/v26 exact-row reuse integration above preserves every original
continuation fit. Its focused component result does not turn this older
profiled run into a normal performance or completion pass.

The unprofiled [v26 B receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v26-on-v1/execution-result.json)
(`cdab91ea`) and
[summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v26-on-v1/observational-summary.json)
(`bd141c90`, trace `1f886982`) report exact 12-bit wire `100000001101` and
plaintext `110101101110`, with pending, identity, source and ordinary physical-END
flags all one. True owner 13 scored END at feed sample 287,296, 6.9567 seconds
after payload endpoint 187,120. TX/channel finished all 148,300 source samples;
capture reached 383,998 with fixed 2 GiB workspace and no health faults. Process
CPU/wall were 39.523558/32.649976 seconds; all launched commands were reaped.
The unchanged strict gate nevertheless failed solely on `primary_assisted=0`:
the legacy-calendar provenance tag deliberately excludes this route. This is
retained as a failed assistance result, not a full qualification pass. A
source-accepted observation-only provenance correction remains private; no
functional arbitration change or passing rerun is claimed.

The unprofiled trace joins 31 true-owner clock records/2,032 jobs, selecting
one of seven LF rates. Its 142 grouped warm measures retain 294,508 original
non-anchor fit visits while evaluating 1,272,604 source-row pairs. These are
executed continuation counters, not FFT or CPU equivalents. Further fresh-RF
warm-work investigation remains private and supplies no future gain claim.

The separate [10 ms profile receipt](../local-evidence/helper-v9/helper-fft-cache-private/normal-v26-on-profile-10ms-v1/execution-result.json)
(`9a10442f`) and
[summary](../local-evidence/helper-v9/helper-fft-cache-private/normal-v26-on-profile-10ms-v1/observational-profile-summary.json)
(`737e82f3`, trace `030e5be0`) retain the same full-wire/END observations and
strict provenance failure, at 41.919658 process CPU/34.855868 wall seconds.
Exact outer Context CPU was 39.497970294 seconds. The profiler classified
3,914 delivered samples, with 165 outside-tag samples and 29 missing overruns
(0.70594%) kept separate, without profiler faults. Control, template-build and
search tags were 52.94%, 27.75% and 16.99% of classified delivered samples,
respectively; these statistical fractions are not exact CPU allocations. The
instrumented template-build call count was 45,577,491.

For each run, the audited complete 235-packet prefix is samples 0–383,278,
26.61653 raw audio seconds including helper lead. Unprofiled prefix CPU was
38.8304–38.9412 seconds over 32.0708 wall seconds, or 1.4589–1.4630 CPU seconds
per raw second; the profiled ratio was 1.5489–1.5532. The final unfinished read
is explicitly excluded. Raw PCM was not retained for bytewise comparison;
profiling overhead and the observed difference are not causally isolated.
Neither run supplies matched OFF/GPSDO performance, payload-only or real-time
throughput, hardware-backlog measurements, or current cable qualification.

The [private continuous 256-bit raw fixture log](../local-evidence/helper-v4/practical-payload-current-prep/private/current-focus-prep/execution-v21-continuous-focus-v1/continuous_focus256.wrapper.log)
(`7b1145c0`) reports exact 256/256 ordinary wire/source bits and physical END
for OFF and ON, without drops or resets. This supported encrypted configuration
uses Fs 6,000 Hz, bandwidth 1,500 Hz, SF 32, T = 0.0426667 seconds, 33 original
pairs, +30 Hz CFO, 100 ppm audio error and matched 2 GiB workspace. OFF reported
118.689465 process CPU/21.233669 wall seconds and 1,421,508 template jobs; ON
reported 44.947931/19.240348 seconds and 360,488 jobs, including 191,924
conditional and 528 observed audit jobs. There was no configured Rate/SNR change.
TX epochs 1800000000/1800000005 legitimately differ with helper lead; each leg
was checked against its own dated wire.

The [outer receipt](../local-evidence/helper-v4/practical-payload-current-prep/private/current-focus-prep/execution-v21-continuous-focus-v1/execution-result.json)
remains exit 1 solely because trace-finalization output interleaved a metadata
CSV line; the actual fixture exited 0, process groups were reaped and postguards
passed. The sealed
[posthoc recovered summary](../local-evidence/helper-v4/practical-payload-current-prep/private/current-focus-prep/execution-v21-continuous-focus-v1/posthoc-recovered-summary.json)
(`32614374`) confirms the same full bits/source/END by demultiplexing that one
metadata line. It reruns no reception and changes no source, binary, phase,
clock or test predicate; the original failed outer receipt remains unchanged.
This v21-bound limited raw functional
pass supplies no production-GUI equivalence, independent next-poll, helper-loss,
long-payload matrix, narrow-HF acceptance, real-time or CPU-per-audio gain claim.

The [A startup receipt](../local-evidence/helper-v9/cold-startup-runtime-private/execution-v20-a-v1/execution-result.json)
(`0d5a7fa8`, [run result](../local-evidence/helper-v9/cold-startup-runtime-private/execution-v20-a-v1/run-result.json)
`5ee0327f`) passed actual frequency bootstrap, retained dates and deliberate
STOP/join. Coarse frequency half-width 25.906 Hz became a qualified half-width
of about 0.12518 Hz. Its complete [trace](../local-evidence/helper-v9/cold-startup-runtime-private/execution-v20-a-v1/trace.jsonl)
(`3dcee65e`) records retirement with 27 remaining blocks abandoned after that
intentional STOP. This scope proves neither ordinary payload reception nor
full A physical END.

The [EME startup receipt](../local-evidence/helper-v9/cold-startup-runtime-private/execution-v20-eme-v1/execution-result.json)
(`cf67e6be`) failed after 21.3472 wall/101.50714 process CPU seconds: the actual
tone never released useful frequency-only startup. That gate failure remains retained;
there is no completed 128-second payload result. Both startup probes retained
unchanged bindings and reaped execution; the failed EME result is not replaced
by the limited A pass.

The subsequent [v21 EME startup receipt](../local-evidence/helper-v9/cold-startup-runtime-private/execution-v21-eme-v1/execution-result.json)
(`3d4a2902`) passed in 8.88758 wall/8.912745 process CPU seconds, with unchanged
bindings and reaped execution. It released 192,000 raw samples into 48,001
logical samples, with zero payload processing and no phase/timing authority.
Optional analysis used 50.844842 ms and reported zero CPU-credit debt. This
qualifies frequency startup/release only, not a completed 128-second EME symbol
or payload, and does not replace the v20 failure.

The separate weak R4 ingest split passed lifecycle/recovery with fresh no-inline
scratch accounting of 2,872 bytes within 2,880, and 216,720 bytes total owned
workspace. `optional-frequency-api-v6-r4-ingest-split/execution-lifecycle-recovery/`
binds lifecycle receipt `d6cad177` and ELF `0ffa4e70`, with unchanged inputs and
reaped execution.
The earlier 2,952-byte scratch failure and punctuation warning-as-error setup
failure remain retained. Separate v6 component cases passed the literal 30/24
dB-Hz streams, metadata process gate, strong controls, coverage and formatted
punctuation. The shifted-tone case with the declared noise sigma failed its
line-476 compound predicate `usable > 0 && first_usable < 3`. The combined
weak-timecode command timed out after 20.0028 seconds. Both outcomes and reaped processes remain in
`optional-frequency-api-v6-r4-ingest-split/execution-existing-cases/`, with
punctuation recovery bound separately in `execution-punctuation-recovery/`.
An instrumented shifted-tone diagnostic subsequently observed 12 intervals
covering the simulated carrier, with half-widths about 0.216–0.228 Hz and none
meeting the 0.1 Hz utility threshold. Its usable count was zero, so the utility
failure remains. Noise-only input passed 31 looks with no published reference
(18.458 wall seconds); this is not a general false-alarm qualification. The
weak-signal diagnostic timed out after 20.0033 wall seconds: seven looks each
used about 3.14–3.46 million work units without a reference, and no final result
for the full 20 audio seconds was reached. These failed/incomplete diagnostics
are retained in `optional-frequency-api-v6-diagnostics/execution-cases/` and
do not establish ordinary payload or END reception.

The older V5 frequency-only EME controls at
[205 dB loss](../local-evidence/helper-v9/optional-frequency-api-v5-qualification/execution/205-result.json)
(`ea4ffa58`) and
[210 dB loss](../local-evidence/helper-v9/optional-frequency-api-v5-qualification/execution/210-result.json)
(`aa7292a0`) both passed and reaped without timeout. At C/N0 30.7609 and
25.7609 dB-Hz respectively, each published 131/131 carrier-covering conditional
reference intervals with no fixture misses. Their half-widths were
0.02246–0.02636 Hz and 0.03710–0.04589 Hz, using eight-second fits over 60 audio
seconds. These unquantized helper-only controls used a coherent Generator
envelope rather than RF retunes, with no ordinary payload and no phase/timing
authority. These fits do not qualify a reference across one complete
128-second ordinary EME symbol, current Live reception or payload completion;
the later weak failures above remain retained.

The optional-frequency API and funded component tests are built, and the later
weak/startup consumers are integrated through v20; weak Live and EME remain
unqualified. Component passes do not establish normal startup or payload work.
Full current-source normal, cable, sanitizer and acceptance
qualification remain pending; original failures and older bindings are retained.

### Preceding v8 build and its limits

The preceding v4 application candidate is v8, overlay SHA-256
`87513574c569de7d9362d498f747cea6658b51e0b681884555242884c2a0e17f`.
`local-evidence/helper-v4/candidate-v8-source-bindings.json` binds 441 inputs
(manifest SHA-256 `5c564afbcfc7f77afd2b186e7fdc2e8c4a0710569259bc8b9fbc5db6d0aa6242`);
`candidate-v8-auxiliary-source-bindings-r1.json` binds 617 auxiliary inputs
(`c2caefc840c1d39eff57ef533ff42fb4c5386ef62b6cde08d3a8479ba2a94639`).
The application build **passed**, exit 0/reaped:
`candidate-v8-application-build/execution-result.json`, SHA-256
`0fe66ddd0bed937089ec5b3898e5bf3335cd8da07217247edeb3ef7a2bcf63f9`.
`candidate-v8-binary-bindings.json` binds six production artifacts
(`96b47a11e68a51fc76ecf3bb06476a6f232acfbe73e5768b60d74fbc73340a18`),
including GUI `8f2af1f46a852a2a8a2afa5dc24ead53d37477f1c3aee2c0cb7c441f8b82dac7`
and core `1872a67255f87398ec623a5d64b18993b4794485ebeb12c83e93edc13d71771e`.

The accepted lifecycle fix retains a helper-qualified accepted owner's original
clock/physical-END scorer when a later independent start competes, including
after reference loss. Independent starts still compete under the ordinary track
cap; no acquisition prior or phase correction is implied. Optional helper
admission yields while ordinary payload is active. Focus **passed three pattern
controls and a deterministic rolling pair** in
`candidate-v8-lifecycle/focused-checks-r1/execution-result.json`, SHA-256
`b555072bf204b2194274756baad921fad2cd4c32c89cbcdc58a85f38288950e7`.
Initial protected-gap focus failure `a7958fd5…` remains preserved; its finite
compact-window setup was corrected before this focused pass.

Five original normal payload checks **passed, 215.458 s total/210.832 s CTest**:
history, physical 64 bits, Live 64 bits, framed text (both entire 1,216-bit wires,
exact authenticated 18-byte source; 45.73 s) and attachment (both entire
2,432-bit wires, exact authenticated 17-byte source/kind/filename; 162.62 s).
OFF/ON retain full raw wire, same pending ID and ordinary END under the original
budgets. `candidate-v8-payload-focused/execution-result.json`, SHA-256
`2c1fbdde1168ded42de1fe988a778e05f28c4888f2d9852c76f2277b4523a9f0`.
These are maintained normal checks, not a current cable/native pass.

The current matched **1,024-bit OFF/ON pair passed** exact source, lifetime
prefixes, stable pending row and physical END (170.488 s pair command).
Reviewed work audit r1 **passed accounting with no unexplained excess**:
`candidate-v8-long-binary-work-check-r1.json`, SHA-256
`92744ff724deb828979fd2b63af7211e2f718b610e1c69a8168216d2a19aeae7`.
It joins actual terminal numeric-owner `simulation_join` execution separately
from completed-feed counts, requiring matching instance/revision/origin and
final maxima; completed-feed definitions remain unchanged. This closes the
initial 12 conditional/template jobs and one FFT. The original failed audit
remains preserved:
`candidate-v8-long-binary-owned-focused/execution-result.json` SHA-256
`193d2c453b13be703400c8bdc428ab97eb68e8591f9cc3925707ed6e03d4b92e`
retains wrapper exit 1/reaped for the failed audit, separately from pair exit 0;
`candidate-v8-long-binary-work-check.json` retains status FAIL (SHA-256
`d69e6355822873cb11fda122d6d8cb3872fa716e87b33a71f8d6c697fe163ed4`),
with original auditor `fcd08553…` retained.

| Current matched profile | Helper OFF | Helper ON |
| --- | ---: | ---: |
| Acquisition template jobs | 5,809,320 | 794,804 = 563,012 conditional +231,792 audit |
| Input FFTs | 44,010 | 48,615 (increased) |
| Process CPU seconds | 494.662448 | 135.234443 |
| Wall seconds | 99.821460 | 69.510709 |

OFF/ON ratios are **7.3091× fewer acquisition jobs, 3.6578× less process CPU,
1.4361× less wall time** in this supported encrypted Auto/rolling profile,
without an RX timestamp, GPS or manual backend selection. Rate/Bandwidth
1,500 Hz, Fs 6 kHz, SF32/T=0.042667 s, crystal/IC-7100 at 30 MHz, controlled
30 Hz CFO/100 ppm audio error/0.502494 degrees/sqrt(second) phase noise;
unchanged payload SNR 35.249387 dB. Matched workspace 4,930,942,976 bytes/bank
3,696,028,208; all three LF alternatives and full stated RF coverage retained.
Acquisition counters exclude continuation/cache work; process CPU includes the
run's helper/observer/filter work. These results establish neither universal,
HF/weak-EME nor real-time throughput gains.

The current named ASan/UBSan focus **passed 15/15 plus Fast CLI 24/24 cases**,
1,559.593 s total (build 618.008 s, CTest 923.667 s), exit 0/reaped.
`candidate-v8-focused-sanitize/execution-result.json`, SHA-256
`06008e8f7915b16714156d5136a5345d808919e1e86ea46cea6e725575fa28a3`.
All four owned groups/children were empty and 441/617/six-artifact guards
unchanged; original ASan/UBSan flags and `detect_leaks=0` retained. GUI test
547.92 s is instrumented test cost, not a real-time qualification.
`helper_normal_loss` passed both continuous/cut cases: exact 64 bits, same
pending row/END, narrowing and phase applied; cut case observed loss and later
admission. **364 other sanitizer registrations were not covered** by this run.
The three direct gap/ordinary-retirement/independent-start sanitizer functions
also **passed**, 23.535 s total, ASan+UBSan symbols verified:
`candidate-v8-gap-sanitize/execution-result.json`, SHA-256
`56404981c822e87c606c1f3a1f2100307b8209178209bc2c638e196c30c1a462`.
All five commands exited 0/reaped with source/normal-artifact/SAN-core/profile
guards unchanged. These selected passes provide no LeakSanitizer or full-suite
pass; the original full-SAN eleven failures remain failed.

The current full seven-scope normal matrix **passed 381/381, freshly selected
exactly once**, with zero failures/incomplete cases, omissions, runtime omissions
or coverage limits. No cases were reused. All nine configure/build/scope commands
exited 0/reaped with empty owned groups/descendants and no cleanup errors.
All 18 binding gates retained source `87513574`, 441 runtime inputs, the same
617 auxiliary inputs plus the separately bound runner (618 qualification
entries), six production artifacts and one unchanged cache-OFF profile.
Fast CLI passed all 24 unskipped unit cases. Total wrapper time: 6,893.159 s.
`final-normal-v8-full-seven/execution-result.json` SHA-256
`b810c8a5edc5b52f3841c119419acf07d12c9b85e493d460add1a8e3dead8e8f`
binds all seven official `normal-{scope}.json` reports and their digests.

| Current normal scope | Passed/selected | Command wall seconds |
| --- | ---: | ---: |
| Core | 325/325 | 1,912.028 |
| Fast | 36/36 | 244.091 |
| Frontends | 3/3 | 0.985 |
| Web | 13/13 | 150.478 |
| Live | 2/2 | 233.574 |
| Preparation | 1/1 | 1,664.442 |
| Calibration | 1/1 | 1,448.306 |

The all-target prerequisite build passed in 1,235.486 s. Core passes include
helper Live assistance 212.63 s, framed text 44.08 s, attachment 165.63 s,
PatternReceiver 55.11 s and HF-edge waveform 10.64 s. Helper normal loss passed
in 67.94 s: both exact 64-bit/pending/END cases, narrowing/phase applied,
cut-case loss observed and later admission. The prior v7 176.699 s Live and
90.0307 s unfinished-source attachment failures retain their failed outcomes;
these new passes do not reclassify their causes.

Calibration completed section `all`, 12 independent workers/64 fixed seeds:
13 modeled cases RMS error 0.024103/max 0.0574951; two shaped cases
0.00842595/0.010376. Its fixed-seed functional assertions contain no CPU/timing
performance assertions; the original 3,600 s CTest deadline was unchanged.
Separate native compilation overlapped Calibration, without other CTest or
audio/device work. Calibration's elapsed time therefore carries a
compiler-overlap limit and supplies no throughput/margin claim.

The native FLTK configure/build/inventory **passed**, exit 0/reaped, in
`build/native-v8-fltk` (552.215 s total):
`native-v8-prep/build-capture-v8/execution-result.json` SHA-256
`bce782a370250a465fbff1b797f77c20e164a0d6ebc40a40b593af7d3c14a8fc`.
The three complete current FLTK native suites also **passed**:
`gui_workflow` 139.762 s, `gui_adapter_conformance` 85.289 s and
`gui_document_conformance` 0.097 s; 226.428 s total, exit 0/reaped, no omissions.
Limited smoke was disabled; original workflow 1,200/1,230 s, adapter 330 s and
document 60 s allowances retained. Final 1,132-input guard preserved normal six
artifacts; three shared libraries match the normal build, with the native GUI
test ELF separately bound. Inventory/native groups and descendants were empty.
`native-v8-prep/execution-v8/execution-result.json` SHA-256
`9b317cf515b550b6d0df1b48938578d75b4b302b8d3e7415ca72a8b5da9333f1`.
This qualifies the three FLTK suites, not Windows/Rev, full sanitizer or release.

The current physical-cable 96-byte text case **strictly passed**, including the
entire 3,648-bit wire, authenticated source, same pending row and scored END.
Frequency narrowing and independently qualified phase/punctuation paths were
observed; all health flags were zero. Maximum backlog **325.855 s** excludes a
real-time claim. The 600 s Controller/610 s recording and reader audit completed
in 617.243 s, exit 0/reaped, with all 441/617/six-artifact guards unchanged:
`cable/candidate-v8-prep/full-long-text96-v1/execution-result.json` SHA-256
`76d21e768d88ea46c99e8801b8a65ef1c991fef0a5d54392c0abbcb278bd161e`;
`reader-audit.json` SHA-256
`d33314149426f670a6331dcf33576d744658f4371168fdabdbfa59c53081fc6c`.
Detailed proof and limits are below. Its reviewed fixture build is
`cable/candidate-v8-prep/build-long-text96-v1/execution-result.json`
SHA-256 `fb6abd3c9d84a5567639e551575c9bb5bef8dcef03f20ef6455be2d1adacae36`;
`fixture-binding.reviewed-v1.json` SHA-256
`c117bd89fa79ceea935a3e4357211e58a58dfe3fdabe0adedd58693058721e20`
binds controller ELF `8df264fdee431ebaa08f52a00ef14d1cea95e439a2eb5d7bb166315c699bb138`.
The initial Python-import setup failure (retained tool receipt `e9be8c`) was
resolved by the proper normal-module import path/invocation only.
The current physical-cable 129-byte attachment also **strictly passed** its
entire 4,864-bit wire, authenticated file, same pending row and scored END,
exit 0/reaped, 919.813 s total. Maximum backlog **607.766333 s** excludes a
real-time claim. `cable/candidate-v8-prep/full-attachment129-v1/execution-result.json`
SHA-256 `c9db2f61119e7409f67082cec714bb470c3d8800427384e27e7ac04f76af9154`;
`reader-audit.json` SHA-256
`32a8442d33313a5d8905b8a86c9bbbd0f86b0aebe2d6785c4cafd4bc15fbddc6`.
Current HF cable testing finished **limited/failed, exit 2**: separate diagnostics
prove the entire eight-bit wire/source identity and scored END, but the strict
full-source pending-progress gates were not met and baseline `search_limited=1`
remains. This is not a strict HF pass. Exact HF simulation and EME metadata-only
results are below. Longer payloads, strict HF cable qualification, Rev, full
sanitizer, full-requested-coverage HF and ordinary EME qualification remain
pending. Prior
v7/e2/f47 results and conditional v7 reuse provide no current-source equivalence
or full-coverage bridge to v8; current normal coverage above was freshly run.

### Prior v7 binding and qualification

The prior v7 application candidate has overlay SHA-256
`27fd861464f760931305b6de74d6986a89dfd2f09684c692c9fce08d97a8e4bc`.
`local-evidence/helper-v4/candidate-v7-source-bindings.json` binds 441 inputs
(manifest SHA-256 `5dfe2e1ebf8eaddb6f022888cacf8186d8d2e62721929431ec31aac09db713ca`);
`candidate-v7-auxiliary-source-bindings.json` binds 617 auxiliary inputs
(`d05e08ec6f407dc55cdf854810d269b1d16a190e824db98e5676cab93545ffbe`).
It integrates the reviewed two-dimensional RF/clock union certificate
(`3df99e84`), calibrated Low-placement fallback (`b4e887c9`) and Fast empty-output
guard (`01cc6e97`); exact patch hashes are in `candidate-v7-integration.json`.

The Release/FLTK application build **passed** with
`./build.sh --web-worker --build-dir build/dev --jobs 2 -- -DDATAPUMP_COMPILER_CACHE=OFF`.
`candidate-v7-binary-bindings.json` binds six production artifacts (manifest
SHA-256 `26181eebbbd0a75219c49082866d60df15e5b0c7cc277dca9f7efd6987904443`),
including GUI `0e027c4f2636e635543f63284340db9fd0194779efee2922ca3c6f0df587a1cd`
and core `cb7c6e2c9eb146180a79016bd933b01176a615ace42726ae8feb862ba329df91`.
The initial build failed on read-only ccache, with unchanged source; preserve
`candidate-v7-application-build/execution-result.json`
(`2957a213d74ad9d59c8f4ed19b228fdb92a00e5d55523a0e1950c44d64f1e76f`)
and its log (`c0e1084e8f9ec22680d78585f9c67254f1b8e8931fe09b2e40d7dabd0eb2c358`).
Cache-OFF recovery passed in 72.733 s;
`candidate-v7-application-build-no-cache/execution-result.json`
SHA-256 `69516d7290cb18ead26888c0ee6e4dac85d1707936332fb792b13553cfe66e0f`,
log `0e0e8e76a54871ef7e78dae1b90d23e312bb28377ad7f03a0539b47203bec504`
and post-audit `da5b9554458ec78f373aa293c77cf4639ec623d699a8262f92e3b92669fcac7f`
bind unchanged inputs/profile,
build completion and cleanup. This is not publication or certification.

Owned CTest inventory captured **381 registered tests** (the prior 379 plus two
new registrations), **without executing tests**; see
`candidate-v7-configured-inventory/execution-result.json` for inventory only.
The fresh five-scope run **failed: 378/379 selected tests passed exactly once**,
exit 1/reaped, 1,964.963752 s: Core 324/325, Fast 36/36, Frontends 3/3,
Web 13/13 and Live 2/2. All official omission/runtime-omission/coverage-limit
lists are empty. All 14 binding gates matched 441 runtime/617 auxiliary/618
qualification inputs, six production artifacts and cache-OFF profile; all seven
owned command groups were clean. `final-normal-v7-fresh-five/execution-result.json`
SHA-256 `4a0484152ad51220f17da39c1c6cdfb1a624cd9c3e64ea46d51359b2fa6be506`,
audit `c4f1e34015d3e01f32ff53d638d79586794cd5558c2e4f25059934302220b51f`.

The sole failure, `helper_live_assistance` (225.795 s), retained exact `001`
pending but no END within the existing 45 s helper-ON rolling low-Fs leg.
Its geometry never admitted a helper cohort/2D certificate, so this does not
establish certificate causation. The failure remains failed: compact log
`final-normal-v7-fresh-five/failed-cases/helper_live_assistance.first-failure.log`
SHA-256 `270db20903fed6d5a2e2e44a9d72f1edbba9115a553fbb4bebd4999452a1950e`,
retained trace `failed-traces/datapump-helper-conditioned-25-174336535161970.jsonl`
SHA-256 `714c47b69d8cd73bc1d9033197ab2536b87c746617626cb55eb2d8bfea0c1514`.
The unchanged existing `--rolling-primary-decline` standalone selector then
**passed** both original 45 s legs, exact `001`/pending/ordinary END, in 67.7146 s
(67.9589 s wrapper). OFF/ON audio was 17.0684/22.8428 s; stdout child CPU
238.882/318.09 s. Only private `TMPDIR` changed; two gates retained all 1,089
inputs/six binaries/profile, with no children or postprocessing errors.
`helper-live-failure-v7-prep/standalone-control/execution-result.json` SHA-256
`382fb9beaf9af3b024e88f44bae63f96c73eaefb553730705cc7f392767b3671`,
log `4ab3547cb5328028491bbf48346aab2633e5ddbabb4c707b1e61cbec37772664`.
That standalone pass remains valid but is insufficient to establish the cause.
The official Core-325 single-job recheck **failed: 323 passed/two failed**,
exit 1/reaped, with no omissions/runtime omissions/coverage limits.
`helper_live_assistance` **failed again, 176.699 s**: OFF passed; the second
rolling helper-ON leg finished simulation with exact `001` still pending and
never reached END. Its retained trace has 1,256 events/zero drops. This rules
out contention as the sole explanation. `helper_payload_tail_attachment`
**failed, 90.0307 s**, with helper-OFF 2,379/2,432 bits pending before its
endpoint: source transmission was unfinished, with no full payload/END pass.
Both failures remain failed, without a throughput-only classification.
`core-v7-serial-prep/execution/execution-result.json` SHA-256
`5ec0ed99d740acf1a2b549058d7921b9a2b9b4656a40b899513209d8a4f771ac`,
`core.wrapper.log` and
`core-v7-serial-prep/execution/reviewer-retained-traces/datapump-helper-conditioned-25-177636366064183.jsonl`.
No source/application changes accompany this recheck.

V7 focused normal checks passed below. Its sanitizer runner was
source-approved but unrun; matched 1,024-bit performance, cable and native GUI
qualification did not complete on that source. Conditional practical reuse of the old normal
Preparation/Calibration coverage was accepted after independent source and
captured-data review. Preparation is byte-identical to a later witness;
Calibration bytes differ, with reviewed changes confined to unused helper-only
paths at helper power zero and no Live extraction. This is retained coverage,
not new execution/timing, original-runtime ELF-hash availability or certification;
the original unrecorded environment remains unavailable. Actual receipts in
`normal-v7-bridge-prep/`: `post-relink-comparison-v2.actual.json` SHA-256
`3d63d7200e3967e69b097d6b251b48ddb27cadc5e335f2e866d1c1729a586848`,
`reuse-acceptance-v2.actual.json`
`9ef727e6f167f9a199b17e5ddd8b0fabfa230678ceff5cc429f321efff21bd04`.
No complete normal bridge/pass is established while Core remains failed.
The prior e2 normal 379-test pass, sanitizer 366/377 result and prior cable/HF
measurements do not establish complete v7 qualification.

The v7 focused normal run **passed all 15 selected CTests with no skips/failures**,
including Fast CLI 24/24 cases, actual helper loss inside the final symbol,
rolling/phase acquisition, HF edge/thin-strip, GUI, fallback, metadata and
simulation controls. The retained regression object was linked to the new core:
Low 0.167 s, gated 0.617 s and full fallback 3.468 s all passed. Six commands
exited 0/reaped in 248.790 s total (13-target build 29.337 s, CTest 212.565 s).
All 14 receipts retained the same 441 runtime/617 auxiliary inputs and six
production hashes. `candidate-v7-focused-normal/execution-result.json` SHA-256
`656457a8cc928a1f526dc28d79d5f0a0dd038243cdcf1e3578a1b81ebdbda16c`,
final audit `1589f83bf08569fd7b6b5f616be1b271b50edf68908f39df87aab55db97d301a`,
JUnit `9c7b7fda8c943dd6d9e64ad5f63e6046b4090a0fb3415e7969c2e344f94e97ca`.
This is selected v7 normal qualification, not a v8, full normal or sanitizer pass.

| Pre-integration evidence for accepted patches | Scope and limits |
| --- | --- |
| RF/clock certificate private controls | All 14 declared command outcomes matched, including deliberate old-geometry exit 2; revised geometry, six thin-strip PCM cases, both mirrored HF-edge PCM cases and unsafe-FP refusal passed. `prior-edge-coverage-proposal/focused-owned-results-v2/execution-result.json`, SHA-256 `b1a656ef86e56fa711d5ba10c1819f9e7e6e8efaaa4ef615cab8565be3c8ba1a`, with final audit. These use the retained e2 witness/private patch, not current v8 qualification. |
| Certificate setup cost/stack | HF setup measured 1.5951 ms versus prior 0.3118 ms; synthetic worst cases 4/6 ms. Normal-profile `clock()` readings were zero/below resolution: **inconclusive**, not a gain. Static selector frame 19,920 bytes versus 1,248, plus 8,464-byte caller; neither whole-chain stack nor runtime high-water qualification. Same private evidence/logs and `.su` snapshots. |
| Low/gated before controls | Both old-library placement errors reproduced in a later pre-v7 witness; `low-placement-before-continuation-v6/execution-result.json`, SHA-256 `9d1429dd0c2bc853ecd37a6afe88862b81a15dff3febc4a8565e09d70bba7f40`. The original over-specific parser failure remains `low-placement-before-v6/execution-result.json` (`e21f2672…`), with partial input/cleanup audit `ba326b3a…`; the retained Low case was not rerun. Corrected v7 Low/gated/full-fallback cases subsequently passed the selected normal checks above. |

### Prior v6/e2 binding and qualification

The prior coherently bound application build is based on
`2e7f3fa61480fbcb82a2a08afd72cf6643cb8bc3`,
with 441 application/build inputs and overlay SHA-256
`e2b951fc5bcec3eddbb3d3bb4e334fc69aea7e6f72c7297df3a7d52029e5e8ef`.
`local-evidence/helper-v4/candidate-v6-source-bindings.json` binds the inputs;
tests and documentation are outside this application manifest. The normal
Release/FLTK application, GUI and worker build passed with
`./build.sh --web-worker --build-dir build/dev --jobs 2`; see
`candidate-v6-application-build.log`, `candidate-v6-binary-bindings.json`
and `candidate-v6-sources.tar.gz` under the same evidence root. Build exit 0,
child reaped; build-log SHA-256
`55b110ed6d137a46c4afa9281e6b7c21cabc3f9548361e31d5fed336016e0093`.
That build's `build/dev/datapump-gui` SHA-256 is
`b043f78739976e4f42c3b431cc049d82396de1e2a42049cc4a0e9e9ed1984133`;
the core archive SHA-256 is
`05cf6181b96d56691d8221f9c818ab331b668d73d82fb58ed109c50ad85670e2`.
This is a working-source/build binding, not publication or certification.
It includes precision-observer/quiet-condition controls, public receive-profile
band conditioning and a broad-audit cadence of max(6 s, eight symbols).
Lease/probation retain max(6 s, two symbols), with the initial FFT aperture
unchanged. It also includes reviewed continuation guard `991e8b`. Maintained
receiver, cover-tail, selected payload/loss and repaired full Live checks passed;
a fresh matched 1,024-bit Auto/rolling pair and independent work audit passed.
Fresh native 96-byte text and 129-byte attachment passed strict full-wire/source/progress/END checks.
The fresh reported HF simulation passed exact-bit/progress/END checks within its
limited ordinary RF bank. The complete normal seven-scope matrix passed 379/379.
Its HF cable result supplies limited GUI completion evidence below. All results
in this prior binding retain their original source and do not qualify v7.

`candidate-v6-equivalence.json` binds this scheduling-only change to prior overlay
`f47fa339fa3b203f25c13c8a0b70b169c716de5016552de9f1f3194c17b816de`:
only `RUN_SERIAL TRUE` for `helper_payload_tail_attachment` changed in
`CMakeLists.txt`; the other 440 inputs and all five production binaries are
identical. Internal 90 s/CTest 240 s limits and assertions are unchanged.
The source archive SHA-256 is
`16aebe2a1c6dd068284f087f80e6b6ea5ffc5a2a2f60484f1654fea3a772de16`.
The `f47fa339` payload, benchmark, cable and HF measurements below are explicitly
reused through that binary-equivalence proof, with no new gain or real-time
claim. Their original `candidate-v5-source-bindings.json`,
`candidate-v5-binary-bindings.json`, `candidate-v5-sources.tar.gz` and application
build log retain their identities; the latter SHA-256 is
`ae0147ec2f467b607aea463842ced610a2236593679af5626ec44a9864772a17`.

The earlier `63fbaaeabe127277ecb74f541ffc9ec8274cf1a6a9c6af2a4c25fa9be1543eca`
build remains bound by `precision-audit-v2-source-bindings.json`,
`precision-audit-v2-binary-bindings.json`, `precision-audit-application-build-v2.log`
and `precision-audit-v2-sources.tar.gz`: GUI
`aae27b6e17ca338ccad3e2126f5aa6db9c3e0c34b7431146c1fae8e66036778c`,
core `fbee489087c7ef4aa775c5779e41bdaf8e464ad4c820d803fa0adcbb5e015c0e`.

Prior `a469ce484c92477348217d3f818bb9d2e75c5bf3c32e698366646024437f8883`
results remain bound by `rolling-qualified-source-bindings.json`,
`rolling-qualified-binary-bindings.json`, `rolling-qualified-application-build.log`
and `rolling-qualified-sources.tar.gz`: GUI SHA-256
`79569093327950ae5a5fb78046f876a8b6c0059752c860dd5022750d525e7f4e`,
core `5d8e25bfa331cb59056e8d260dacd00e0b7492ff8ee34d723cc5669844cb640e`.
Those reception/performance/cable results do not qualify the later build.

| Source-bound qualification and retained failures | Result and binding |
| --- | --- |
| Original broader normal run, `f47fa339` | Full prerequisite build **passed, 1,188.057660 s**. Core selected 323: **318 passed/5 failed**, CTest exit 8. Failures: attachment capture deadline and four disposable-GPG-key setup cases below. Overall run was **interrupted, exit 130** after Core, during Fast: seven cases passed before `fast_transfer` cancellation. Preparation/Live/calibration/frontends/web were unrun. `final-normal-v5/execution-result.json`, `normal-core.json`, preserved JUnit/logs; owned children reaped, all 441 inputs/five binaries matched. No complete normal-matrix pass. |
| Focused recovery and scheduling-only `v6` | Host `tests/test_ci_apt.py` **30 tests passed, 10.068 s**; isolated unchanged-binary `helper_payload_tail_attachment` **passed, 167.35 s**, both original 90 s legs/assertions intact. Independent review accepted only `RUN_SERIAL TRUE` for that fixture; coherent rebuild passed and all production binaries remain identical. All four focused GPG cases then **passed, exit 0, 26.982416 s**, strict owned-descendant/process-group cleanup complete. `focused-host-ci-apt-v5.log`, `focused-serial-attachment-v5.{log,xml}`, `candidate-v6-equivalence.json`, `focused-host-gpg-v6/execution-result.json`. |
| Complete normal regression, `e2b951fc` | **379/379 passed exactly once across all seven official scopes**, exit 0/reaped, zero failures/omissions/runtime omissions/coverage limits: Core 323, Fast 36, Frontends 3, Web 13, Live 2, Preparation 1, Calibration 1. Scope-wrapper wall times: 1,167.97/290.14/1.01/150.36/234.72/1,675.30/1,432.410 s; total wrapper 4,956.355 s. Core attachment ran alone for 167.80 s with both exact 2,432-bit/END legs; all four formerly failing GPG fixtures passed. Inventory audit covers 379/379 registered tests once; all 18 source/441-input/five-production-binary/profile bindings match, all nine groups ended with no owned/adopted descendants or residual process group, no cleanup error. `final-normal-v6/execution-result.json`, SHA-256 `1675a40e3482176112e3338a88019e543d44debe310444d360c2a341ddf2a7ba`; official `normal-{core,fast,frontends,web,live,preparation,calibration}.json`, JUnit/logs retained. |
| Normal calibration | Section `all`, 12 independent capture workers/64 fixed seeds per matrix case; original assertions passed. Matrix RMS error **0.024103**, maximum **0.0574951**, 13 modeled cases; shaped RMS **0.00842595**, maximum **0.010376**, two modeled cases. `final-normal-v6/normal-calibration.log` and official report; this is the stated calibration profile, not general RF qualification. |
| Prior mixed-Live output-placement gap, `e2b951fc` | A valid Low-helper selection with unavailable geometry could fail the whole transmission before helper fallback: planning preceded playback fallback, while disabling helper required a plan. The old fallback test expected whole-TX failure, so the 379-test pass did not establish the [documented unavailable-placement fallback](helper-carrier.md). V7 integrated separate calibrated `data_gain` and placement/minimum-rate fallback; selected corrected Low/gated/full-fallback checks **passed**. Full-scale/unscaled ordinary PCM would invalidate RF calibration and rate/past-epoch expectations. Broader v7 qualification remained incomplete. |
| Prior assisted RF/clock coverage gap, `e2b951fc` | Center-only RF translation could clip qualified reference uncertainty just inside a resource-truncated edge. The supported +30 Hz measurement remains bound to its stated case. V7 integrated a two-dimensional union certificate over original RF × clock cells, using physical A and authoritative beta, retaining every LF alternative, recentering the full strip and failing closed where unsupported. Private and maintained selected edge/thin-strip normal checks passed; **no complete v7 normal/performance qualification**. |
| Sanitizer build and matrix, `e2b951fc` | ASan/UBSan configuration **passed, 0.667 s**; the two-job instrumented build **passed, exit 0, 7,700.597 s**, completing all 782 Ninja tasks. `final-sanitize-v6/instrumented-build-bindings.json`, SHA-256 `b159a462cd75ffee1dc4af2170f47c6d48d57f4dc7008d53108eee86cf918936`. Runtime qualification **failed: 366/377 passed, 11 failed/incomplete**; Core/Fast/Live/Preparation/Calibration failed, Frontends/Web passed, wrapper exit 1/reaped. All eleven original failures remain failed, including the actual UBSan finding below. Final provenance/selection/cleanup audit passed separately; no full sanitizer pass. Desktop native GUI and Rev remain **unrun/held**. |
| Final sanitizer selection/provenance audit | **Passed**: 377 registered/377 selected exactly once across seven official reports; all official omission/runtime-omission/coverage-limit lists empty. All 18 source/441-input/auxiliary/profile bindings matched; 15 checks of the 308 instrumented artifacts matched their binding; all nine command groups reaped with no owned/adopted descendants or residual groups. Seven report hashes/statuses verified. `final-sanitize-v6/execution-result.json`, SHA-256 `1117a3bb8b7bb3dfedd8bdebd72dca8c3bcb27d54178c67110276b5e45781548`. This audit qualifies evidence integrity/selection, not the eleven failed assertions/workloads. |
| Official sanitizer Core result | **Failed: 321 selected, 315 passed, six failed/incomplete**, zero omissions/runtime omissions/coverage limits. Scope wrapper 4,267.957 s, official wrapper exit 1/CTest exit 8; post-Core bindings and owned-process cleanup passed. `final-sanitize-v6/sanitize-core.json`, SHA-256 `58e133d97b88daeba287c8c1313c5d5421bfed25474a171bdbe4fa34442df0ef`, and `execution-result.json`. The six original failures remain failed; none reported a sanitizer diagnostic. |
| Other completed sanitizer scopes | **Fast failed, 34/36 passed**, 922.485 s wrapper, official JSON SHA-256 `ccdb17f67f8d27866a6e739233a437b6aaeb7e9b4f7b15a56e50e307e26d4d22`; **Frontends passed 3/3**, 8.142 s; **Web passed 13/13**, 162.605 s (`web_live` 137.98 s); **Live failed, 1/2 passed**, 816.697 s (`live_profiles` passed 484.60 s). All have zero omissions/runtime omissions/coverage limits. `final-sanitize-v6/sanitize-{fast,frontends,web,live}.json` and `execution-result.json`; final bindings and owned-process cleanup accepted. Production inputs/profile/binaries remain frozen. |
| Original sanitizer cover-tail timeout | `pattern_private_cover_tail` **timed out, 180.14 s** under the 180 s CTest watchdog. Preserved stdout is empty/buffered, with no assertion or sanitizer report. Source review finds a deterministic offline test, 11 receives/289,931 samples, no sleeps or internal deadline. A separate unchanged-binary 900 s diagnostic **passed all 11 controls in 465.821 s**, recorded below; the original qualification remains failed. Original records in `final-sanitize-v6/failed-cases/`: `pattern_private_cover_tail.LastTest.log.tmp.txt`, SHA-256 `1cf78d184aedee057c061adec2cc79fc4518d7a4c0989af6a5830ef5678ff5fa`; `pattern_private_cover_tail.wrapper-record.txt`, SHA-256 `f5dc77d56604cdad0e912bd95540143b31f619415aa175af4c449e92d323504d`. |
| Original sanitizer bounded-workload failures | **Failed**: `helper_live_assistance`, 48.01 s, “full assisted payload failed exact long-text/attachment decoding”; `helper_payload_tail_framed_text`, 60.78 s, lost full payload/progress/physical completion in the helper-OFF baseline (no ON leg); `helper_conditioned_native`, 148.40 s total, bounded non-Simulation capture did not finish its 90 s fault leg. No sanitizer error was reported. Source/trace review shows premature workload stops below; these original runs establish no full-bit/source/END success or conclusive final-codec error. Larger-budget private diagnostics passed separately below. |
| Original sanitizer attachment deadline | `helper_payload_tail_attachment` **failed, 90.49 s**, at its internal 90 s allowance, not the 240 s CTest watchdog. OFF baseline retained 799/2,432 pending bits on ID 1/revision 1, feed 262,502 before physical end 782,810 plus six seconds of absence; no ON leg. No full payload/source/END result is established. Preserved compact log and raw trace are bound below. |
| Original sanitizer GUI stop failure | `gui_controller` **failed, 171.58 s**: “Ending fake FHSS did not restore the configured frequencies and idle label.” In `fake-eu`, transmitting/stopping remained true after 10.0005 s wall time; source advance since stop was 2.7 s, below the protected tail's 3.14681 s. No sanitizer diagnostic. The unchanged selector failed again; a private 30 s diagnostic passed separately below. Source caps and immediate-abort timers are unchanged. `final-sanitize-v6/failed-cases/gui_controller.LastTest.log.txt`, SHA-256 `ba9140021528c1fac76e991114949070bca06811c568d092f7c1b8125100544e`. |
| Original sanitizer Fast failures | `fast_cli` **failed, 102.97 s**, with actual **UBSan null-pointer argument to `fwrite` at `src/fast/codec.cpp:35`**, for clear/encrypted empty-text saves (two failures among 24 subtests). `fast_snr` **failed, 450.91 s**: nested 300 s `TimeoutExpired` for `fast_regression --profile acoustic --bytes 1024 --seed 417`; no acoustic completion or statistical pass. Preserved logs/hashes and unintegrated repair scope below. |
| Original sanitizer Live failure | `live` **failed, 331.80 s**, waiting for default media-clock transmission at the unchanged 40 s normal/120 s instrumented allowance. Only 1,143/1,216 bits were generated, fraction 0.92818, feed 162,095, compute 120 s; health flags and receiver/error strings were clear. This is incomplete workload evidence, not a completed payload/END or codec pass. The exact-allowance selector failed again; a standard-geometry control passed separately below. |
| Original sanitizer Preparation timeout | **Failed, 0/1 passed**: `scheduled_parallel_preparation` timed out at 9,000.22 s (scope wrapper 9,000.535 s). Reservation policy and default/fp32/fp64 serial/parallel comparisons completed; timeout occurred during `shared.serial`. Shared comparison, small/tiled work and cancellation/no-false-END checks remain incomplete or unrun. No sanitizer diagnostic in this scope; no raw-trace coverage claim. `final-sanitize-v6/sanitize-preparation.json`, SHA-256 `8b36700b9a901e730dbb550999ad372f622346e082638eeb56d83c67a675530e`. |
| Original sanitizer Calibration timeout | **Failed, 0/1 passed; timed out at 10,800.21 s**, command wrapper 10,800.5952 s, with the original 12 workers/section `all`, no declared CTest timeout and unchanged 10,800 s fallback. Last output reported matrix RMS 0.024103/max 0.0574951 over 13 modeled cases, then the first shaped public-transition result 41/64 (predicted 0.630249). Second shaped, 32 null receives and four default captures remain incomplete/unreported; worker location is not established. No sanitizer line preceded timeout; no calibration pass. `final-sanitize-v6/sanitize-calibration.json`, SHA-256 `b5facf67963156d0ff88abbbc69f516cff93e0ddd208f054ee97a4ef39252e9c`. |
| Repaired complete Live assistance | **1/1 passed, 219.30 s**, exit 0/reaped, after accepted test-only start-refinement repair `1a9bbf`; fixed/full framed, retained-backend and beacon-fallback cases all reached their assertions. `continuation-guard-live-coordinate-repair-ctest.log`. Original failed runs remain recorded below; no fresh cable/full-matrix claim. |
| Prior `f47fa339` matched Auto/rolling binary | **Both passed**, entire 1,024-bit physical wire/post-XOR source, lifetime prefixes, stable pending row and ordinary scored END, zero health flags and matched workspace. Independent audit: jobs **7.309×**, process CPU **3.748×**, wall **1.362×** lower; FFT count increased. `candidate-v5-long-binary.log`, `candidate-v5-long-binary-bindings.json`, `candidate-v5-long-binary-work-check.json`. |
| Integrated continuation guard | Scoped review accepted `991e8b`; focused build passed. Maintained `pattern_receiver` **1/1 passed, 59.36 s** (CTest total 59.37 s); `pattern_private_cover_tail` **passed, 69.02 s**, retaining its 180 s allowance and preservation-contract registration. `continuation-guard-focused-build.log`, `continuation-guard-maintained-build.log`, `continuation-guard-pattern-receiver.log`, `continuation-guard-focused-ctest.log`. These selected checks do not themselves qualify cable or the remaining sanitizer/native/Rev scopes; the separate historical cable and normal-matrix passes are recorded above and below. |
| Original maintained group | **9/10 passed, 536.99 s, exit 8**; sole failure `helper_live_assistance` at 177.24 s, detailed below. Passing tests: cover-tail 69.02 s; history 0.01 s; physical 64-bit off/on 1.60 s; Live 64-bit off/on 0.77 s; authenticated 18-byte text/1,216-wire-bit off/on 47.12 s; authenticated 17-byte attachment/2,432-wire-bit off/on 170.22 s; phase continuity 0.05 s; rolling acquisition 3.19 s; normal helper loss 67.74 s. Full exact wire/source, pending identity and scored END assertions passed in the selected payload cases. `continuation-guard-focused-ctest.log`; this original result remains 9/10, followed by the separately recorded repaired Live pass. |
| Prior Live fixture failure | **Failed, 177.24 s** on an overly strict physical-start premise: primary ID 2 refined first sample 8,129→8,134→8,129 across revisions 1→2→4, with exact prefixes `0`→`00`→`001` then complete, without extra bits. Independent review confirmed stronger ordinary aliases may refine the start across revisions. Accepted test-only fix `1a9bbf` preserves exact within-revision start and all lifetime bit/prefix/END gates, allowing no numeric tolerance; subsequent full Live run passed above. Failure retained in `continuation-guard-focused-ctest.log` and `continuation-guard-fresh-rolling/datapump-helper-conditioned-25-119401817807183.jsonl`. The earlier actual `001000` detector witness remains a separate failed result. |
| Earlier rolling-decline fixture | **Failed, exit 1**; primary ID 1 at actual epoch 1,792,891,250/first sample 2,832 retained exact `001`, pending then complete. Separate ID 2 at epoch 1,792,891,256/first sample 8,286 published prefix `1`, triggering the test's blanket secondary-row assertion. Minimal test repair `ae4ce3` was independently accepted; strict actual-dated primary-lifetime parser unchanged. The subsequent group advanced past this assertion before the distinct start-refinement failure above. `continuation-guard-rolling-primary.log`, complete trace with 780 attempted records/zero drops, `continuation-guard-fresh-rolling/datapump-helper-conditioned-1-118922602534843.jsonl`. |
| Prior precision/public-band/audit controls | **3/3 passed, 4.22 s** at `63fbaaea`: `helper_receiver` 1.95 s, `helper_receiver_conditioning` 2.21 s (49 controls), `helper_phase_continuity` 0.05 s, covering conditioning controls, public-band union and audit arithmetic. `precision-audit-v2-receiver-ctest.log`. No ordinary weak-EME acquisition or payload gain is established. |
| Initial `63fbaaea` Live integration group | **9 passed/1 failed, 199.06 s**: sole failure `helper_live_assistance`, calendar fixture's strict revision assertion. Preserved trace records same row 2, exact 12 bits and END across increasing revisions 2/5/7/9. Test-only repair `2520133a` retains exact prefix/ID/start/count/no-reopen gates while permitting increasing revisions; the fresh run below passed that calendar control but failed later. `precision-audit-v2-live-ctest.log`, `precision-audit-v2-calendar-failure-{trace.jsonl.gz,commits.json}`. |
| Fresh assistance control | **Passed, 1.33 s** with rebuilt `helper_assistance`; `precision-audit-v2-assistance-repair2-ctest.log` (CTest total 1.34 s). This does not clear the separate Live failure. |
| Prior Live detector failure | **Failed, 171.17 s** (CTest total 171.19 s). The twelve-bit calendar control passed with exact reception/pending/END, 126 conditional jobs and 21 periodic-audit jobs. A later conditioned case extended true primary row 2 from expected `001` to `001000` through dated candidate replacement; “conditioned acquisition changed an accepted bit prefix.” Paired native controls below reproduce the extension with the helper component removed; the first private guard failed ordinary-payload preservation. `precision-audit-v2-live-repair2-ctest.log`, retained `precision-audit-v2-primary-extension/{datapump-helper-conditioned-25-116152918182876.jsonl,selected-commits-and-control.json}`. This original run remains failed; later guard/fixture results are separately recorded above. |
| Final GUI/geometry checks | **2/2 passed, 6.41 s** at `a469ce48`: `gui_application` (5.68 s) and `helper_normal_acquisition_geometry` (0.73 s). New GUI regression `4219dd7a`; `rolling-qualified-focused-ctest.log`, `rolling-qualified-focused-bindings.json`. |
| HF fixture revision guard | **Geometry 1/1 passed, 0.72 s** after test-only secondary-revision/manual-observation correction, fixture `558098ca`, binary `b8d87bde`. Runtime `a469ce48` unchanged; `rolling-qualified-fixture-repair-build.log`, `rolling-qualified-fixture-repair-focused.log`. |
| Core focused group | **9/9 passed, 83.26 s** at `0a020b1a`, whose core archive is byte-identical to the historical `a469ce48` build (`5d8e25bf`). Startup guide, replay staging, phase continuity, rolling acquisition, cohort recovery, thin strip, mock native-provider helper loss (67.72 s), geometry and GUI application; `rolling-reference-focused-ctest.log`, `rolling-reference-focused-bindings.json`. Maintained source-row/HF regression `6f357f9c` passed; manual fixture `10fe3bea` built. |
| Matched ordinary Auto/rolling binary | **Both passed** at `a469ce48`: all 1,024 physical-wire/source bits, exact pending progress and ordinary END. Independently audited acquisition jobs fell 7.309×, process CPU 3.734× and wall time 1.355× in supported profile L below; FFT work increased. `rolling-qualified-long-binary.log`, `rolling-qualified-long-binary-bindings.json`, `rolling-qualified-long-binary-work-check.json`. |
| Long framed text | **Passed** at `a469ce48`: the same varied 257-byte source and entire 7,296-bit wire, authenticated source recovery, exact prefixes, stable pending row and ordinary scored END. This closes the reproduced text regression. `rolling-qualified-long-text.log`, `rolling-qualified-long-text-bindings.json`. |
| Prior `f47fa339` native Live/duplex framed text | **Strict pass** at `f47fa339`: entire 3,648-bit typed physical wire and exact authenticated 96-byte text, pending row 2/revision 1 through ordinary END, 11,510 retained GUI pending observations/zero prefix failures and 6.050667 s scored absence. Zero health/search-limit flags, complete zero-drop light trace, clean PCM; no numeric-work or real-time claim. `cable/candidate-v5-long-private/candidate-v5-full-text96-v1/result.json`. |
| Prior `f47fa339` native Live/duplex attachment | **Strict pass** at `f47fa339`: exact authenticated 129-byte `cable-tail-129.bin` and entire 4,864-bit typed physical wire beyond the 4,096-bit preview; same pending/completed row 2/revision 1, 19,575 retained correct prefix observations and 6.039333 s scored absence. Primary helper acquisition and qualified frequency/phase paths observed, with later bounded phase fallback; all health/search-limit flags zero. `cable/candidate-v5-long-private/candidate-v5-full-attachment129-v1/result.json`. No numeric-work or real-time gain claim. |
| Prior `f47fa339` exact reported HF simulation | **Passed `decoded_exact`, exit 0/reaped** at `f47fa339`: all eight physical-wire bits exact and post-XOR source `11010110`, lifetime prefixes, stable pending row, ordinary scored END and later assisted admissions, encrypted Auto/rolling/unknown timestamp. Helper 8,181→51 pairs retains all nine LF values; baseline `search_limited=1` remains (requested +/-90.45 Hz versus represented +/-7.979848 Hz). No full-domain or real-time claim. `candidate-v5-hf-eight.log`, `candidate-v5-hf-eight-bindings.json`. |
| Prior `f47fa339` HF native cable | **Limited GUI completion, not strict qualification**: observed exact eight-bit source `00101101` on ID 2/revision 7, stable after first completion through the 900 s window. No eight-bit pending poll; strict same-pending completion/all-prefix gate remains false. Controller exits 2 (`search_limited=1`), runner exits 5 after the 256 MiB trace cap; no full physical-wire/END-coordinate or work-gain pass. `cable/candidate-v5-full-hf8-on-v1-result.json`. |
| Prior `a469ce48` HF native cable | **Incomplete, exit 2** after the 900 s window: maximum 7/8 pending bits, no complete payload/END. TX passed; ordinary baseline `search_limited=1`, other health flags zero. `cable/rolling-qualified-full-hf8-on-v1-result.json`. |
| Prior `f47fa339` native long-source preflights | **Both passed, controller/runner exit 0**, source/linked/key unchanged, complete zero-drop traces and zero health/search-limit flags. Text 96 bytes/3,648 wire bits and attachment 129 bytes/4,864 wire bits; no TX requested in preflight. Both full cases subsequently passed separately. `cable/candidate-v5-long-private/candidate-v5-preflight-{text96,attachment129}-v1/result.json`. |
| Prior `a469ce48` exact EME observation | **Cancelled incomplete, exit 130** before helper onset: 43,008 samples/7.168 s audio. No helper measurement, final Snapshot/health, payload or benefit qualification. Strong, precise helper assistance for keyed compact acquisition remains an unfinished capability; `rolling-qualified-eme-observation-cancelled-bindings.json`. |
| Other payload/attachment configurations, HF native/full requested coverage, EME, desktop native/Rev and sanitizers | Remain pending or unqualified. Each cable pass retains its stated binding and does not qualify weak RF, broader hardware conditions, real-time throughput, the unrun desktop native/Rev scopes or the incomplete sanitizer matrix. |

The sanitizer profile registers **377 tests**, compared with normal's 379:
Core 321 (318 in its first phase, then isolated `gui_timing_advice`,
`gui_controller` and `gui_application`), Fast 36, Frontends 3, Web 13, Live 2,
Preparation 1 and Calibration 1. Only `gui_self_check` and
`build_speculation_codegen` are absent registrations: [CMake](../CMakeLists.txt)
guards the former on `DATAPUMP_BUILD_GUI` (off in this headless executable
profile), and the latter on `NOT DATAPUMP_SANITIZERS` for its optimized witness.
Both passed in the normal 379-test matrix. They are not sanitizer passes or
runner omissions; no registered tests are explicitly excluded, and realtime
fixtures were explicitly included with `--sanitizer-realtime`: `fast_session`
passed in 37.6384 s and `gui_fast_live` in 117.16 s. External dependencies and
LeakSanitizer are outside the instrumented build's stated qualification.

The failed full-payload workload reached 2,277 pending bits/feed sample
1,466,548, versus at least 8,193 wire bits/physical end 5,252,130 plus absence.
The framed-text OFF workload stopped at 331/1,216 bits/feed 112,793, before
physical end 393,690 plus absence. The native fault leg processed 8.8 s of audio,
before its first later bit at approximately 9.92 s (6 s later burst, at least
2 s helper lead-in and 1.92 s training); five resets and replay had already
executed. All three original results remain failed. Raw traces are preserved in
`final-sanitize-v6/failed-traces/`; compact records in
`final-sanitize-v6/failed-cases/` are respectively
`helper_live_assistance.LastTest.log.tmp.txt` (SHA-256
`ca02c0433ee31881300f31e5009528e2e30300f685aa7371a24f4ee6042d9f86`),
`helper_payload_tail_framed_text.LastTest.log.tmp.txt`
(`6470c856e2f803f006c42e100df2f11c7c2116130f78326a4079e63bc931ea68`)
and `helper_conditioned_native.LastTest.log.tmp.txt`
(`47a1da37a7fe7e769107a99aa00219c46d0190e1f0c9cef763582621945df872`).
The attachment record is `helper_payload_tail_attachment.LastTest.log.tmp.txt`
in the same failed-case directory (SHA-256
`553452c5d23538a17bb5bc68f71f5d6b469a99f78127ba208ce04793d2db5aff`);
its raw `failed-traces/helper-tail-rolling-2-off-1-143249657179016.jsonl`
SHA-256 is `67007c9f95773050afc678238fc8f38bc23c1cb315cc8c9cf870dedbb242c0e0`.
Three private C++ diagnostic copies only increase known finite outer-workload
allowances 12× and add a full-function selector; all assertions, keys, clocks,
recovery and source caps remain unchanged. Their four selected runs passed
below. Production source/profile/binaries remain frozen; these private results
waive no original failed result.

All following diagnostics retain `e2b951fc`; source/linked bindings and owned
process cleanup passed, with no sanitizer diagnostic. They do not qualify v7,
the complete registered suites, external libcrypto or LeakSanitizer.

| Separate post-matrix diagnostic | Result and final small-metadata binding |
| --- | --- |
| Unchanged cover-tail, 900 s allowance | **Passed, 465.821 s**, all 11 receives. `private-cover-tail-diagnostic-v6/execution-result.json`, SHA-256 `58c7f08385171b2c9d128e946215ea322512c22cdef91c3c385e0adec7453d49`. |
| GUI unchanged/private allowances | Original 10/5 s selector **failed, 26.9589 s**: EU stop reached 10.0056 s wall but only 2.9 s source tail, below 3.14681 s. `gui-fake-hop-diagnostic-v6/execution-result.json`, SHA-256 `7e9fabf0c6858fbff383ed10fe461c0c6b035d135cf647cf4daecb1879d6d777`. Private 30 s diagnostic **passed**: EU stopped at 12.9488 s, protected tail 3.18854 s ≥3.14681 s and ≤3.26806 s source cap; idle/frequencies/health restored. `gui-fake-hop-private/execution-v6-30s/execution-result.json`, SHA-256 `90d18bdb8eaa1572bc0769582a58b9614600768914388928fbf57f81536450f8`. Original failures remain failed. |
| Live unchanged/control | Exact 40/120 s selector **failed, 121.607 s**, framed transmission 1,143/1,216 bits unfinished, health clear. `live-media-clock-diagnostic-v6/execution-result.json`, SHA-256 `bd0b5e34a67f7c81e4242115206238eeac1766b00d80bcafa120ceabfdc2c3dd`. Standard positive-geometry control **passed, 31.485 s**, all three modes/cancel/Clear/error, without workload warning; `live-media-clock-standard-diagnostic-v6/execution-result.json`, SHA-256 `5aba50cb1da1d7b7b0d328b8322136408e6a5a6f5bb0b7fedb54907ef3a28d9f`. This control is not compact-geometry repair or full Live qualification. |
| Private helper workload bundle | **Four passed**: full payloads 371.366 s (1 KiB framed text/257-byte attachment/17-byte constrained case; progress prefix checks cap at 128); paired framed text 507.360 s (both entire 1,216-bit wires, exact 18-byte authenticated source, pending identity/physical END); paired attachment 1,928.717 s (both entire 2,432-bit wires, exact authenticated 17-byte source/kind/name, pending identity/physical END); native fault/replay 165.729 s (233,760 delivered samples equal committed frontier, healthy recovery). ON phase path active, committed narrowing false; no gain claim. `helper-workload-diagnostic-private/execution-v6/execution-result.json`, SHA-256 `50f7b78adf67d0f7a78cddd19565f642586952c6dd84861c81a7767c1cfd39a7`. |

The prior e2 Fast empty-save defect matches unchanged HEAD `src/fast/codec.cpp`
(SHA-256 `82b60fb649f6aa511543591d00da7d20e7aaeb1eff9e3d577b4310161239c377`).
A one-line empty-output return patch, SHA-256
`01cc6e9781cd6c5dc59a6262a6c23170b43614e3a14be5d3f2672151360e2a23`,
is now integrated in v7, with fresh qualification pending. The actual e2
UBSan log is `failed-cases/fast_cli.LastTest.log.tmp.txt` under
`final-sanitize-v6/` (SHA-256
`b943ec9cacb8adb60db43182e144e6fa922bdee42e4093c7930364f82c9cf3fa`);
the timeout log is `failed-cases/fast_snr.LastTest.log.txt`
(`80b9b4950ca72a6d88255b46064bde1a3dad5fbb9f53f55d3bd9543c41bc138c`),
and Live is `failed-cases/live.LastTest.log.tmp.txt`
(`cbf064d495b3a3b5ab8cea7c20882ae15119ed9fa354c6dd336d8254eaa62c0d`).
The unchanged e2 GUI/Live selectors above preserve the exact 10/5 s and 40/120 s
allowances and remain failed. Later v7/v8 integration and selected results keep
their separate source bindings above.

The Preparation failure preserves complete compact records in
`final-sanitize-v6/failed-cases/`:
`scheduled_parallel_preparation.LastTest.log.txt` (SHA-256
`37bba9df6e29f2a1ab62f25e679d36b6f6f584c3d8965e1517b212bb87c797f8`)
and `scheduled_parallel_preparation.wrapper-record.txt`
(`5f9fcc51493dc27ff132382373eebf543c45f48672711601e646585310d46d9c`).
No shared raw-trace path was recorded. The three completed arithmetic trace
paths (`scheduled-parallel-{147323345692032,150282567771886,153197409874211}.jsonl`)
are unavailable; their logged finalizations do not establish retained raw-trace
coverage. These diagnostics retained frozen e2 inputs; the Low-helper fallback
and Fast empty-output corrections were subsequently integrated in v7.

Calibration compact records are also retained in the failed-case directory:
`differential_receiver_probability.LastTest.log.txt` (SHA-256
`a1508ebf6864358ba89ff84aa8020c64655fa11ab39a867a3bda5d6ae06d6949`)
and `differential_receiver_probability.wrapper-record.txt`
(`1c0f98f19a54282194f47afc6cabb7553863699041ac56116ff7b28738c2460d`).

The earlier private Low-placement proposal `fe9` is superseded: a valid ordinary
FHSS gate allowance can cross DC before its cochannel-calibration catch. Reviewed
v2 is under `local-evidence/helper-v3/placement-fallback-v2-proposal/`, patch
SHA-256 `b4e887c93a7a2f220a5d572dd3a00dea371d374accf29ec7f60c1187260b303f`,
recipe `3263551d543da3b5fb9bb11cc96573b02598bbdbd2dc91c000d32e590a7aca44`.
Separate Low/gated selectors retain exact fresh source/wire/key/PCM/duration
checks. Old-library controls reproduced both failures as recorded above;
selected corrected v7 runs passed the focused normal check. The complete normal
379-test pass remains bound only
to `e2b951fc`. The v6 native wrapper is obsolete and held; v7's configured
five-scope selection is 379 of 381 registered tests, failed 378/379. Old normal
Preparation/Calibration coverage is conditionally reused within the practical
limits above, without a new execution or complete normal-pass claim.

Unless explicitly prefixed otherwise, evidence paths below are relative to
`local-evidence/helper-v4/`. Results retain their original source/binary bindings;
the historical `f47fa339` pair below retains its own performance binding and does
not qualify v8; the current v8 measured comparison is above. Prior `a469ce48`/`6cb27953` measurements
remain separately bound.
The historical `0a` application build passed with an unknown-pragma-option warning
retained in its log. The later historical `a469ce48` changes corrected that scoped
warning-selection typo and false GUI narrowing inferred from independent mixed
receiver maxima. That `a469ce48` build no longer emitted the unknown-option
diagnostic, and its DSP core remained byte-identical to `0a020b1a` (`5d8e25bf`).

### Assistance contract and interpretation

The common-link rolling architecture supersedes v3's fixed-timestamp/Complex-only
policy. Qualified helper measurements condition ordinary Auto/rolling acquisition,
including newly admitted key/profile/epoch candidates, with bounded original-domain
audits and fallback. The ordinary decoder, RF/LF alternatives, supported backend
policies and payload functionality remain authoritative. Presence or predicted SNR
alone does not qualify frequency narrowing, phase, timing or UTC. Helper carries
no payload; expected lengths are test assertions, never receiver completion inputs.

Helper-only lead/tail is dwell-aligned and at least two seconds on either side of
the complete Robust waveform, extended for buffering and fully scored absence.
Helper loss cannot supply payload END or discard buffered accepted bits. Qualified
phase correction is causal and retains an affine phase convention on dequalification.
Punctuation can support that model without collapsing private epoch/start or
audio-clock search. Full framing, decompression, recovery, exact next-poll progress,
stable pending-row identity and six-second fully scored absence remain ordinary
receiver responsibilities. The cases below establish only their stated scope.

UI presence, active narrowing and applied phase remain distinct; Link Planner
conditional estimates are not measured CPU margin. The configured
[receiver budget](receiver-strategies.md) of 0.6 is elapsed processing seconds per
audio second, a selection objective, not a CPU-core limit. Process CPU/audio ratios
measure core use; helper lead/tail also affects whole-run wall/audio ratios. Neither
implies an extra gain, payload-SNR/probability boost or general real-time guarantee.

### Repairs and focused controls

| Source/repair | Actual result, original failure and limit | Evidence |
| --- | --- | --- |
| Earlier helper-control lifetime beyond 240 admissions | All 21 private compile/check commands passed; unchanged 3,360-byte ABI/layout offsets. Matched public-Session baseline: 248 admissions/resident receivers, sticky global decline, zero prior callbacks. Candidate: 254 admissions/250 resident, two prior callbacks/decisions on rows admitted after ordinal 240, four conditional jobs. Separate unchanged recovery cases both retained exact 24 bits and ordinary END, including optional failure and repeated ordinary failure/reset. Controls cover shorter-period joins, reference loss, departure cleanup and admission rejoin. Boundary probe stops boundedly; no full-payload, idle-retirement or throughput claim. | `cohort-unbounded-private/evidence-manifest.json`, SHA-256 `613a25503bf356137dcc9ad99c7b35572e6753ca0bc399fee64f20b87185805f`, with source/binary/command/trace bindings. Idle-row retirement was neither reproduced nor changed. |
| Earlier exact unmodulated source-row reuse | All 114 nonprimary cells matched both original bit scores bitwise; two complete 16-bit PCM/noise-tail flows had byte-identical every-poll transcripts and ordinary scored END. Actual-HF interleaved-DSSS, nine clock groups: 80 nonprimary cells/two scores bitwise. RF/LF alternatives and logical trials retained; no pruning or 8,181-lane throughput claim. Unsupported 18-clock-group reuse declines without score/cache mutation. | `continuation-base-private/evidence.json`, actual-HF appendix and `continuation-base-private/hf-run.log`. Missing `/usr/bin/time` setup failure and initial cancellation-responsiveness failure remain retained. Reviewed per-cell stop check passed stop/reuse repair; the original assertion's stale-cache-tag label was imprecise. Diagnostic durations are not calibrated gains. |
| Prior-refresh guard `d44a28a5` | Private actual-class PCM `001` plus END unchanged: 19 jobs, 11 transforms, 5,196 trials, 581,943/627,738-byte working/reserved peaks. Eighteen duplicate refreshes preserved caches. No elapsed-gain attribution. | `prior-idempotence-private/run.log` and bindings. |
| Replay staging `18aeeb8c`, `test_live` `1b5efac3`, built at `965dd7b3` | Original refusal: 92 accepted events/349,342 bytes in a 351,760-byte pool. Corrected: nine rows/540 pending events + nine terminals at 98,852 bytes peak; 25 rows/1,500 pending + 25 terminals at 259,668 bytes. Frozen selector and injected allocation-failure atomicity/drain-retention/retry passed. These are private actual-class controls, not full text completion. | `staging-timeline-private/`, original refusal and `*-reviewed*.log`. Header/probe ASan/UBSan 25-row pass in `staging-timeline-private/timeline-peak-current-asan.log` used Release core/dependencies and leaks off; not whole-Live sanitizer coverage. |
| Thin strip / overlap arbitration | Old mask's 150 ppm spacing bound replaced by original-builder 600 ppm cells, preserving all clock values and nearest-frequency ties. Getter 3/233 was insufficient: same PCM ordinary `10110010`+END became only helper suffix `0110010`; true bit scored 230.828 over threshold 55.7531, vetoed by an earlier overlapping spurious published track. Corrected matched check retained full eight bits and separate six-bit spurious track through ordinary END. Six strict cases passed, 330 conditional jobs each, clock +/-300 ppm and CFO -0.05/0/+0.05 Hz. Layout/bit quotas unchanged; working bank 675,080→675,069 bytes including mask, optional diagnostic records 128→118. | Original `thin-strip/diagnosis/`; repaired `core/overlap-history-funded/` and `core/overlap-history-final/`. |
| Accepted-owner lifetime / GUI widening | Pattern `4e080763` failed; `4382d4fe` passed startup and helper-owner controls, and strict thin-strip controls again. Unknown helper-involved peaks cannot withdraw an accepted owner; ordinary replacement remains effective. GUI widening `src/live_receptions.hpp` `4795ffc6` passed private arbitration with row IDs retained. | `core/unknown-owner-replacement/` old/corrected bindings; `gui-widening/`. These are not whole-Live or CPU-margin qualification. |

| Source-bound focused result | Outcome and preserved setup/failure scope | Evidence |
| --- | --- | --- |
| `63fbaaea` prerequisite build | Initial focused build was **cancelled, exit 130 before compilation**, to serialize the application build. Subsequent serial focused build passed; this cancellation is not a runtime/test failure. | `precision-audit-v2-focused-build.log`, `precision-audit-v2-focused-build-serial.log`; application build/binary bindings above. |
| `56c93b10` | 9/9 passed, 82.20 s; same nine selectors as the then-selected core group, helper loss 67.40 s. Initial unknown `test_live_replay_staging` target failed before compilation/tests; corrected `test_live` build passed. Optional `transmitter_seed_committed` (`src/live.cpp` `c1844bbf`) proves first logical native callback commitment, not DAC delivery; no receiver timestamp, hook API, payload or END change. | `native-seed-focused-ctest.log`, `native-seed-focused-bindings.json`, original `native-seed-focused-build.log`. |
| `6cb27953` | 8/8 passed, 14.63 s: same group without helper loss. | `idempotent-owner-focused-ctest.log`, `idempotent-owner-focused-bindings.json`. |
| `965dd7b3` | 3/3 passed, 6.14 s: staging, geometry, GUI application. One-byte/13-bit diagnostic observer passed in 8.414 s with exact wire/source, prefixes, same pending row and END. Initial fixture `bc4becc5` failed an inappropriate framed-auth gate for dictionary text; test-only `b339e5e6` uses ordinary `short_text_decoded`, preserving long authentication assertions/runtime. Borrowed hook Live `8a3491d5`/private `4fc22521` supplies diagnostics, never RX length/END. | `timeline-owner-focused-ctest.log`, `timeline-owner-focused-bindings.json`, `timeline-owner-observer-short.log`; original failure retained. |
| `f49a3b2a` | All 19 prerequisites built. Coherent group: 10 passed/1 trace-path-collision setup failure, 18.42 s, after recovery's 24-bit/reset/END cases passed. Path-only fixture `84238ddb` passed twice from same CTest directory, 0.95 s each, unchanged assertions/runtime. Other passing selectors covered startup/arbitration/staging/thin-strip/rolling/geometry/filter/fallback/timing-continuation/GUI. | `preserved-owner-focused-build.log`, `preserved-owner-focused.log`, `cohort-path-{first,second}.log`, `cohort-path-bindings.json`; not an eleven-test pass. |
| `5958ec74` | 4/5 passed in 7.47 s: rolling 0.63 s, recovery 0.96 s, staging 0.01 s, GUI 5.35 s; thin strip failed 0.52 s. Recovery covered stop/fresh work. | `rolling-cells-focused.log`. |
| Older `bc763` | 6/6 passed in 98.90 s: `helper_live` 17.69 s, `helper_output_fallback` 2.53 s, `helper_filter_response` 0.98 s, `helper_sim` 3.05 s, `gui_link_planner` 69.27 s, `gui_application` 5.36 s. | `final-focus-small.log`; selected group, not full contract/shared-GUI/native-display coverage. |
| Earlier rolling phase/guard fixture | Passed 0.50 s after two test-only HF guard-coverage setup repairs; nonzero real-carrier image, phase-weighted outer-guard covariance, wrong-seed control. Original failed assertions retained. | `phase-outer-covariance-final.log`. |

### Broader normal attempt and focused recovery

In the original concurrent Core run, `helper_payload_tail_attachment` reached
all 2,432 bits but remained pending when its internal 90 s deadline expired.
With a 320-sample symbol at 2 kHz, capture 790,880 versus physical end 782,810
left only 8,070 samples/4.035 s of input tail, below the required 6.08 s of fully
scored absence. Pending was correct; the receiver did not use expected length
to complete. The isolated same-binary rerun passed both legs in 167.35 s with
the original 90 s per-leg allowance, CTest 240 s limit and assertions intact.
Only this fixture's CTest scheduling changed to `RUN_SERIAL TRUE`.

The other four Core failures were disposable-key setup failures because
`gpg-agent` IPC was unavailable: `build_ci_apt`, `build_arch_release`,
`build_gentoo_sync` and `build_apt_release`. The focused host CI-APT run passed
all 30 cases, establishing host IPC capability. The four exact GPG fixtures then
passed in `focused-host-gpg-v6/execution-result.json`, with strict owned-process
cleanup, and all four also passed in the subsequent 323/323 normal Core scope.
The normal restart completed all seven scopes, 379/379 tests with no omissions;
the original Core reports/JUnit, interrupted Fast log and failed outcomes remain
under `final-normal-v5/` and are not relabeled as passes.

### Prior primary-extension diagnosis and guard qualification

The guard measurements bind `f47fa339`, including scoped-reviewed continuation
guard `991e8b`; prior `e2b951fc` has identical production binaries as proved above. Private probes below
reproduce and bound the `001`→`001000` failure; their successful diagnostic exits
are not whole-Session reception passes. Evidence is under
`precision-audit-v2-primary-extension/`.

| Diagnostic | Actual result and limit | Evidence |
| --- | --- | --- |
| Paired actual native row | **Reproduced**, 28.383797 s, exit 0: all 513 original RF/LF pairs/three LF values; identical composite PCM versus helper-component-removed PCM both publish physical wire `100000`/plain `001000`, first sample 8,031/end 15,896. Original three source bits map to wire `100`. Neither leg completes by sample 24,000; both traces have zero drops. Source includes 3,082 cover samples; Robust data is zero after EOF +128 samples. The physical 956 Hz helper aliases to 68 Hz outside the data band. The extension therefore reproduces without the helper component; this is a causal control, not a functional pass or proof all channel effects are resolved. | `single-row-baseline.log`, `single-row-run-result.json`, `single-row-baseline-{on,off}.jsonl`; unchanged bindings, child reaped. |
| First private guard `a4349`, patch `66ce` | Initial cover case and nine controls passed in 70.184020 s, but the guard was **rejected** by ordinary-payload preservation. At 40 dB SNR, RF offset 0.125/T (0.390243902 Hz), clock 150 ppm, phase diffusion 0.15 degrees/sqrt(second), symbol 328 samples: original retains all eight bits +END; guard loses bits. A valid raw score about 106.728 falls to 45.005 below threshold 50.780. No production integration. | `guard-paired-summary.json`, `guard-run-result.json`, `guard-highsnr.log`, `guard-highsnr-run-result.json` and paired traces/bindings. |
| Full-Session attempts | **Incomplete**, no reception pass: OFF exits 2 at 50.042118 s; ON times out/killed at 41.952053 s. Earlier OFF setup exits 3 at 12.044163 s. | `run-repair-results.json`, `fixed-repair-{off,on}.*`, original `run-results.json`/`fixed-off.*`; no throughput comparison. |
| Earlier continuation-guard diagnostic | Paired 40 dB midpoint RF/LF/phase controls retain eight bits +END; the diagnostic then **exits 1 at 71.630516 s** during the -5 dB nominal OFF baseline, “ordinary row changed exact stream positions during drain.” This original baseline/assertion outcome remains failed and establishes neither guard causality nor production reception failure. | `continuation-run-result.json`, `continuation-paired.log`, `continuation-*` source/binary/compile bindings and traces. Later scoped review/integration does not replace this outcome. |
| Established weak continuation preservation | Corrected impaired private control **passed, exit 0, 3.372642 s**: 19 full wire bits, identical every-poll progress and physical END off/on; 18 weak symbols at -11 dB, final-symbol gain 8, T=1 s, RF offset 0.125 Hz, clock 150 ppm and phase diffusion 0.15 degrees/sqrt(second). Both prove first joint acquisition and an established joint continuation before the strong symbol, 13 target confirmations. Scope is this paired continuation control, not full Live/low-SNR coverage. | `established-weak-run-result.json`, `established-weak-impaired.log`, paired traces and `established-weak-*` source/binary bindings. Original first-joint-premise experiment **failed, exit 1, 3.121957 s**, retained in `established-weak-nominal-assessment.json`; its read-only exact-19-bit/progress/END assessment does not establish the missing fresh-joint premise. |

### Ordinary Auto/rolling payload and measured work

All six pairs below used the ordinary encrypted Auto/rolling path, default epoch
clock, unknown TX date/receive timestamp zero, no GPS or decoded-timecode authority,
and matched per-pair workspace. Each primary recovered its entire actual dated
wire and source, full exact prefixes, stable pending row and ordinary scored END.
Helper lead legitimately changed TX epochs `1800000000`→`1800000005`; each leg
was checked against its own wire. No primary/secondary rows were merged and no
false-alarm qualification follows. No receiver-health failure was recorded.

| Profile / binding | Geometry and impairments | Row, coverage and evidence scope |
| --- | --- | --- |
| L: prior `f47fa339` 1,024 bits | Rate/Bandwidth 1,500 Hz, Fs 6 kHz, SF 32, T=0.042667 s; crystal/IC-7100 at 30 MHz Shift, 30 Hz CFO, 100 ppm audio error, 0.502494 degrees/sqrt(second) phase diffusion. Loss 130 dB, data 4 W/helper 1.5 kW, unchanged payload SNR 35.249387 dB. Matched workspace 5,456,820,224/bank 4,090,436,144 bytes, resolved once. | Three selected rows/two secondary each; helper 33→3 pairs, all three original LF cells/full coverage retained; exact full dated wire/source, every lifetime prefix/pending/END and zero health flags. Peak DSP 1,372,208,175 off/1,372,669,115 on. `candidate-v5-long-binary.log`, `candidate-v5-long-binary-bindings.json`, `candidate-v5-long-binary-work-check.json`. |
| L: prior `a469ce48` 1,024 bits | Same L geometry/impairments/SNR; matched workspace 5,726,044,160/bank 4,292,354,096 bytes. | Three selected rows/two secondary each, all completed rows scored END; helper 33→3 pairs with all three original LF values/cells and full coverage retained. Zero health flags. Peak DSP 1,439,514,159 off/1,439,975,099 on; helper resources 4,940/7,168 bytes. `rolling-qualified-long-binary.log`, `rolling-qualified-long-binary-bindings.json`, `rolling-qualified-long-binary-work-check.json`. |
| L: prior `6cb27953` 1,024 bits | Same L geometry/impairments and unchanged SNR; matched workspace. | Three selected rows/two secondary each; helper 33→3 pairs, all three original 600 ppm clock cells. `idempotent-owner-long-binary.log`, `idempotent-owner-long-binary-bindings.json`, `idempotent-owner-long-binary-work-check.json`. |
| L: `f49a3b2a` 1,024 bits, fixture `15d510f2` | Same L geometry/impairments and unchanged SNR; matched workspace. Cohort/optional phase storage 7,108/7,168 bytes. | Three selected rows/two secondary each; 33→3 pairs, all three clock values/cells; later admissions. `long-auto-binary.log`, sealed traces, `long-auto-binary-bindings.json`, `long-auto-binary-work-check.json`. |
| F: `f49a3b2a` 64 bits, fixture `617c4041` | Rate/Bandwidth 1,000 Hz, Fs 6 kHz, SF 32, T=0.064 s, DSSS off; same RF/link/errors/SNR as L (phase diffusion about 0.50249 degrees/sqrt(second)). First Controller workspace 6,222,331,904 bytes frozen both legs. | One selected primary/no selected secondary each; raw suffix events retained. Helper 49→3 pairs with all three 600 ppm clock cells. `fast-auto-smoke.log`, complete traces, `fast-auto-smoke-bindings.json`, `fast-auto-smoke-work-check.json`. |
| P: older `bc763` equivalent, 64 bits | Rate 1,000, target 22, Fs 6 kHz, 1 MHz Shift, crystal/IC-7100, DSSS off, data 4 W/helper 1.5 kW; SNR unchanged 5.249387 dB, CFO 1 Hz, audio 100 ppm, phase diffusion ~0.5025 degrees/sqrt(second). Normal Controller/settings with private initial-media hook. | Separate secondary completed rows held 34 bits off/24 on. Helper 49→6 pairs retained three clock centres, but **150 ppm mask cells understate required 600 ppm** (declared domain 300 ppm): not full continuous-clock or current-performance qualification. `practical-integrated.log` and complete traces. |

| Pair / helper | Template jobs | Partial excess | Input transforms | Process CPU (s) | Wall (s) | Captured samples |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Prior `f47` L off | 5,809,320 | 0 | 44,010 | 480.734253 | 87.396753450 | 335,863 |
| Prior `f47` L on | 794,792 | 0 | 48,614 | 128.263774 | 64.160263459 | 367,748 |
| Prior `a469` L off | 5,809,320 | 0 | 44,010 | 475.134704 | 86.240001592 | 335,863 |
| Prior `a469` L on | 794,792 | 0 | 48,614 | 127.247332 | 63.646948091 | 367,748 |
| `6cb` L off | 5,809,320 | 0 | 44,010 | 477.913906 | 86.212767362 | — |
| `6cb` L on | 794,792 | 0 | 48,614 | 126.967540 | 63.109436616 | — |
| `f49` L off | 6,005,076 | 0 | 45,493 | 500.793365 | 91.201260812 | 335,863 |
| `f49` L on | 794,792 | 0 | 48,614 | 127.336050 | 63.010002409 | 367,748 |
| `f49` F off | 857,304 | 392 | — | 68.844975 | 11.903233350 | 98,682 |
| `f49` F on | 225,056 | 72 | — | 25.429580 | 9.404495397 | 130,376 |
| Older P off | 939,820 | — | 2,740 | 726.936 | 104.459 | 271,849 |
| Older P on | 164,612 | — | 3,002 | 139.961 | 41.266 | 303,542 |

The historical `f47fa339` independent audit confirms 5,809,320→794,792 completed
acquisition jobs: 563,000 conditional +231,792 broad-audit jobs with helper on.
Reductions are 7.309233× template jobs, 3.748013× process CPU and 1.362163× wall;
input transforms increased 44,010→48,614. All 76 off/77 on numeric instances have
joined records, and all-record maxima equal maxima over every joined
`incomplete=0` record for all seven counter classes, including records preceding
final incomplete snapshots. Cancellation excess, decreases, nonnumeric owners
and uncovered admissions are zero; identities covered are 71/71 off and 77/77
on, with zero unclassified helper-on acquisition jobs. Both complete traces have
zero drops/caps, 6,220,935 bytes off/158,695,306 on. The matched wrapper exited 0
in 152.151761 s, child reaped, all 441 inputs/binaries/fixture/key unchanged.
See `candidate-v5-long-binary-work-check.json` and
`candidate-v5-long-binary-bindings.json`.

Audit `candidate-v5-work-audit.py` (`5c7a74`) reused the retained counter method,
without old expected totals or gain ratios. The first outer timing wrapper
failed **setup, exit 127**, missing `/usr/bin/time`, preserved in
`candidate-v5-long-binary-work-audit.log`. The Python resource/performance wrapper
then ran the unchanged auditor **successfully, exit 0/reaped, 3.522966 s**;
`candidate-v5-long-binary-work-audit-attempt2.log` and
`candidate-v5-long-binary-work-audit-attempt2-result.json` retain this result.

The prior `a469ce48` independent audit confirms 5,809,320→794,792 jobs, including 563,000
conditional +231,792 broad-audit jobs with helper on: 7.309233× less reported
template work, 3.733946× less process CPU and 1.354975× less wall time. Both
traces report complete seals with zero drops/capping, retained at 6,220,903 bytes
off/158,696,862 bytes on; wrapper exit 0, reaped,
unchanged source/binary/key/fixture bindings. All 76 off/77 on numeric instances
have joined records; maxima over every record equal maxima over **every**
`incomplete=0` record, including earlier joined records before the 60 final
incomplete helper-on snapshots. All seven counter classes have zero cancellation
excess, no decreases or nonnumeric-owner records, and no uncovered admitted
identities (71/71 off, 77/77 on). The helper-on conditional/audit partition closes
with zero unclassified acquisition jobs. Source/trace hashes and per-instance
results are in `rolling-qualified-long-binary-work-check.json`; the independent
command/log are `rolling-qualified-work-audit.py` and
`rolling-qualified-long-binary-work-audit.log` (exit 0, 2.623 s).

These are acquisition counters, not a total-operation or cache-build audit.
The `f47`/e2 `src/pattern_receiver.cpp` `991e8b14`, and prior `4dd1d041`, deliberately
retain full RF/LF continuation fits with exact source-row reuse outside
`template_jobs`/`input_transforms`; the new cover guard and source/template cache
work are also outside these acquisition counters.
Cache/preparation fields are not emitted, and unavailable-work events are filtered
from this fixture trace; admission-to-numeric coverage is checked rather than inferring
absence from a missing filtered event. Measured process CPU is separate broader
processing evidence. The result establishes a supported normal-use comparison,
not secondary-row false-alarm, hardware or exact reported HF/EME qualification.

Previously audited L/F jobs are completed/joined, with partial excess reported
separately; P retains its original executed-work count. The corrected `6cb` audit takes each instance's
maximum from **every**
`incomplete=0` record, including completed records preceding a final incomplete
record. All-record/completed maxima agree. The two prior L helper-on runs include 563,000
conditional +231,792 broad-audit jobs. F all-record maxima are 857,696 off/225,128
on; its partial excess is not completed coverage. P on includes 120,708 conditional
+43,904 audits. Measured reductions (jobs / process CPU / wall) are `6cb` L:
7.309×/3.764×/1.366×; `f49` L: 7.56×/3.93×/1.45×; F: 3.81×/2.71×/1.27×;
P: 5.7×/5.19×/2.53×. L and P FFT work increased, so not all work fell.
These are supported-profile, source-bound measurements, not cache-attributed gains,
exact 36 Hz HF/weak EME, hardware throughput or a general budget guarantee.
F helper-on CPU/audio was ~1.17 cores; P was ~16.0 off/~2.77 on, with the budget
interpretation above.

The F/L fixture phase flag omitted `receiver_helper_cohort_phase_applied`.
Full `f49` traces contain 1,483/14,455 timing-qualified phase-work events,
respectively, first at later epoch `1800000016`; this proves work executed where
qualified, not primary phase accuracy/benefit or UTC alignment. Counts/first dates
remain in `fast-auto-smoke-phase-check.json` and `long-auto-binary-phase-check.json`.
P recorded no cohort phase-applied events. P's measured binaries bind overlay
`e1856143` in `phase-staging-practical-binary-bindings.json`: only mock-test
composition in `CMakeLists.txt` differed from the older 440-input manifest, with
runtime inputs unchanged. `bc763c2e` source/GUI `d22ef780` retain their identities
in `final-source-bindings.json` and `final-binary-bindings.json`.

### Live/duplex, helper loss and calendar controls

| Binding and case | Actual result and limits | Evidence |
| --- | --- | --- |
| Integrated guard, mock native loss | **Passed 67.74 s**: continuous helper and helper cut both exact 64 bits, pending row 2 through scored END, narrowing and phase applied, 42 admissions including later epochs. Cut 64,643 lies inside the final payload symbol ending 64,960, about 79 ms earlier at logical 4 kHz; loss observed only in the cut case. This is mocked native-provider coverage, not new cable qualification. | `continuation-guard-focused-ctest.log`, `helper_normal_loss` stdout and complete zero-drop trace seals; successful fixture removes temporary traces. |
| `f49`, connected cable, 64-bit off/on | Both passed all 64 exact prefixes and same-row physical completion, encrypted Auto/rolling timestamp zero; no health errors, row violations or clipping. Rate 200, target 55, crystal/IC-7100, 30 MHz Shift, data 4 W/helper 1.5 kW, Fs 6 kHz; hardware 8 kHz off/48 kHz on, so no cable-throughput claim. Helper 233→3; 847 timing-qualified phase-applied records, first reference 13,705 behind frontier 13,800 at input 13,920. Phase-path coverage, not accuracy. 110 s/48 kHz S16 peaks 5,451 off/5,228 on, zero clips. All 441 inputs, linked binaries and keyfile unchanged; seals/cleanup complete. | `cable/preserved-owner-full-main64-{off,on}-v1-*`, results/prefixes/bindings/traces/WAV. |
| Older `bc763`, connected cable, 64-bit off/on | Controller/runner both exit 0, all exact prefixes/same-row ordinary END, no health failure; unchanged source/linked/key hashes and cleanup. Same additional cable settings, DSSS off, Volume 20, T=0.32 s; hardware 8 kHz off/48 kHz on. Adjacent helper 2,212.5 Hz around 1,500 Hz data, lead 2.0–2.4 s/tail 7.24–7.64 s around 25.720 s waveform; helper pending/received, 233→3 pairs, no cohort phase events. Raw audits remained; no CPU/work gain. 110 s mono 48 kHz S16 peaks 5,449 off/5,230 on (5,230/32,768; 0.1596 FS), zero clips/recorder warnings. Old 150 ppm cell bound does not qualify continuous-clock coverage. Harness linked GUI ELF but did not launch native window. | `cable/final-main-off-ready-*`, `cable/final-main-on-ready-*`; success JSONs losslessly archived `.json.gz`, original/decompressed hashes in `cable/final-main-ready-results-archive-map.json`, raw traces/WAV unchanged. Original `cable/final-main-off-*` controller 4/runner 2 **setup failure**, “ordinary binary transmit remains disabled after normalization,” before TX request; silent recording/logs retained. Ready-gate wrapper corrected; not observed production TX failure. |
| `f49`, mock native loss | Passed 67.80 s, continuous and helper cut both exact 64/prefixes/same pending row 2/scored END; narrowing/phase applied, 42 epoch admissions including later ones. Cut 64,643 before payload END 64,960 (~79 ms at logical 4 kHz); loss observed only cut case. Paced/resampled 48 kHz PCM provider mocked, not cable. Fixture `0b772adf`; successful fixture deletes temporary traces, detailed sealed stdout retained. | `preserved-owner-normal-loss.log`, `preserved-owner-normal-loss-full.log`, `preserved-owner-normal-loss-bindings.json`. |
| Earlier mock native loss | Passed 67.72 s, same exact 64/prefix/row/END/new epochs with narrowing/phase and dequalification. Helper-only cut 317 logical samples (~79 ms) before final waveform end, paced 48 kHz resampled PCM, default epoch clock/unknown TX date. Constant CFO is not weak-HF phase-tracking qualification. | `normal-loss-corrected-last-test.log`, continuous/loss traces `normal-loss-corrected/`. |
| Earlier complete `helper_live_assistance` | **Failed 61.47 s** in calendar helper-on: 375,000:1 ratio put helper/FIR at 3,437.5 Hz beyond fixture Nyquist, no cohort. Fixed-timestamp 1,024-byte text, 257-byte attachment, constrained 17-byte cases reached passing assertions first; whole test still failed. Original failure/crash retained. | `calendar-failure/`. |
| Valid-passband one-bit calendar | 3/3 original pairs, measured nonzero origin, 21 alignment jobs/full bit+scored END at about 45.55 s, no pair reduction; **failed extra pending assertion** because first FFT was already 25.57 s after bit END/no earlier accepted bit. Pending-assert removal scoped only to this one-batch case; 64-bit pending gates intact. Later `f49` one-bit **failed broad-work assertion**: ready 45.55 s, periodic due 73.995 s, capture 69.57 s. Older `5958` reference-loss broad work was incidental, not periodic evidence. | `calendar-failure/pending-assert-rolling-cells-calendar.log`, `calendar-one-batch-pending-test-only.patch`, `preserved-owner-calendar.log`. |
| `f49` strengthened calendar, fixture `21e13651` | Three-bit `101` passed, unchanged receiver length knowledge/deadlines: first 45.551 s, exact pending/same-row END with 14.22 s fully scored absence; 42 conditional +21 actual periodic audits, new row at 3.672 s. All 3 clock cells/pairs retained, no pair-count gain. Helper-off did not acquire in 94.668 s capture. | `calendar-three-bit.log`, `calendar-three-bit-bindings.json`. |

### Exact reported HF and EME configurations — scopes and limits

The HF crystal/IC-7100 geometry is Fs 6,120/T 14.222222 s with 8,181 ordinary
pairs: native ordinary coverage +/-7.979848 Hz versus requested +/-90.45 Hz,
full clock coverage but limited frequency coverage. The simulated +30 Hz RF error and
audio-clock term yield about +30.15 Hz at data carrier. Helper translation of the
conditional origin is supported; the raw cap alone proves neither exclusion of
that offset nor assisted coverage. The 5.8 GHz EME GPSDO-OCXO encrypted Auto/rolling
geometry is Fs 6 kHz, bandwidth 1 Hz, SF 64, 768,000 samples/128 s per symbol,
3,565 pairs/five clock values with full
declared +/-1.7400009 Hz coverage. Current v8 and prior `f47` HF simulations passed
full primary wire/source/progress/END checks within their stated scopes;
the earlier `a469ce48` run remains failed despite
its separate posthoc trace pass. Strict native HF completion evidence and full
requested frequency coverage remain unqualified. EME cases have not demonstrated
completed reception or useful reference precision.

Current v8 source analysis permits translated catalog origins of 30/80 Hz with
all nine LF alternatives, but the separate all-pair 17-bin guard limits
|origin| to approximately 82.02015 Hz in this HF geometry; it does not establish
the full requested +/-90.45 Hz domain. The actual private HF-edge PCM cases
were at +/-7.99985 Hz, not 80 Hz. The current v8 runtime pass below uses +30 Hz;
it does not qualify 80 Hz or the full requested domain.

Current exact-case execution completed with both scoped results passed, unchanged
441/617/six-artifact guards and clean owned groups:
`exact-v8-prep/owned-execution-v8/execution-result.json`, SHA-256
`8816a324adedd82bf0f4acebc2d8cc79d7e6def3045b01e53b39a8acae62e988`.
The EME scope is metadata only, not ordinary DSP reception.

| Binding/case | Actual result and qualification limit | Evidence |
| --- | --- | --- |
| Current v8 exact HF helper-on simulation | **Passed `decoded_exact`**: all eight source bits/entire dated wire, prefixes, same pending row and scored END; Rate 36/interleave 10, crystal/IC-7100, controlled +30 Hz CFO/100 ppm audio error, encrypted Auto/unknown timestamp. Original 8,181→49 selected pairs, all nine LF alternatives retained. Fixture-observed 147,560 jobs =90,293 conditional +57,267 audits; 264 FFTs. Three GUI rows/two secondary, phase assistance zero; no false-alarm qualification. 1,016,893 samples, peak DSP 1,819,116,884 bytes; process CPU 1,777.029994 s/wall 468.071951 s. Baseline `search_limited=1`, all other health flags zero. **No real-time, matched HF-OFF CPU-gain or full-RF-coverage claim.** | `candidate-v8-hf-eight.log`, `candidate-v8-hf-eight-bindings.json`, SHA-256 `e1cb8a82fdf45ff2bb0591b3bd931d4a1064c78c4a6296196aebbc6a561cb386`; complete zero-drop trace `candidate-v8-hf-eight/hf-on-bits-189332173563753.jsonl`. |
| Current v8 HF native/cable preflight | **Limited/failed, session/audit exit 2/reaped**, no TX by design: sole health failure is baseline `rx_search_limited=1`; full clock coverage, limited frequency coverage, supported existing Complex backend, resource grants admitted/filter frontend ready. Error/overrun/drop/reset/interruption zero. This preflight does not show that helper narrowing is unavailable. | `cable/candidate-v8-prep/preflight-hf8-v1/{execution-result.json,reader-audit.json,observation/}`; execution SHA-256 `5f76e5b7ea5b876680488b1e214bc8d1bb6b04442a2028c79a54da0e57a205c3`. Build exit 0/reaped, `build-hf8-v1/execution-result.json` SHA-256 `aa5394434f1f5f4c3c5e5fb9547fb682eceba2cb27191a870d9caf9490ea143a`; reviewed fixture receipt `4d5337b330368977fccf301cd567f2d076cf7a128004dc253604177b73d450e8` binds actual ELF `b9f307cfc60d9a3ceb2740f81327c06e2c753c063e936e6bf45cff385c7fd432`. |
| Current v8 HF native/cable helper-on, full 900 s Controller/910 s recording | **Limited/failed, session/audit exit 2/reaped** (`limited_or_failed`), not a strict pass. Separate completed-source diagnostics prove entire eight-bit wire/source `00101101`, source identity/canonical phase/seed joins and ordinary physical END on row 2/revision 7/candidate 2: first 20,794/end 717,148/feed 853,185 at 6,120 Hz; required scored absence 87,040 samples, all completed-row END failures zero. **No full eight-bit pending state** was observed in GUI or shared commits: strict primary ID 0, same-pending/completed and all-prefix gates false. Native accepts were 2,3,2,final 1 bit with completion; following shared commits exposed 2/5/7/8-complete on ID 2. Source/trace review found no publication gap: the final bit and already-scored END were accepted together and immediately committed. The full-eight-bit pending predicate remains unsatisfied coverage; gates and failed classification are unchanged. Baseline search-limited flag 1 remains; all other health flags zero. Frequency assistance observed (original/selected maxima 8,181/220), phase inactive. | `cable/candidate-v8-prep/full-hf8-v1/{execution-result.json,reader-audit.json,observation/}`. Execution SHA-256 `1c1e61ff22ff2f279396267614e3880d501265dad419c0c50052e812d58c2173`; reader `f1257e59f1845a09cf33b3c23f487d28fb6c263f89a35f26face12d6b7e94513`. TX passed 167.306644 s, source 116.955719 s; helper lead 2–2.400163 s/tail 46.545915–46.946078 s, initial FFT 45.525 s. Outer/session/audit 920.434186/911.258678/8.235592 s. Complete uncapped trace, 521 attempted/zero drops; 910 s/48 kHz S16 PCM peak 2,695, zero clips/XRUN warnings. Maximum backlog **774.169935 s: not real-time**. All 441/617/six-artifact guards unchanged, owned groups/children empty. Snapshot job counters are not numeric work/gain proof; no CPU, weak-RF or production-memory admission claim. |
| Current v8 exact EME geometry | **Metadata-only pass**: Fs 6,000 Hz/B 1 Hz/SF64/H 768,000/T 128 s, 3,565 pairs/five LF values. At 75%, Controller-resolved workspace 9,671,310,336 bytes and projected bank 7,251,303,728 bytes. No receiver bank, waveform, DSP acquisition/completion or ordinary EME payload was executed; these figures do not qualify allocation or throughput. | `candidate-v8-eme-geometry.log`, `candidate-v8-eme-geometry-bindings.json`, SHA-256 `be0b6e14528eb0d37bbd41273f88a67d575fcda8784b671fa901f769f5e67ad5`. |
| Prior `f47fa339` exact HF helper-on simulation | **Passed `decoded_exact`, exit 0/reaped**: TX epoch 1,800,000,003, eight exact physical-wire bits and post-XOR source `11010110`; complete/contiguous wire, exact source/lifetime prefixes, pending identity, primary completion/ordinary scored END, all historical completed-row END gates and later assisted admissions. Primary ID 2/revision 8/candidate 2, first sample 19,128/end 715,414; END commit feed 853,185 (input gap 137,771 samples/6,120 Hz =22.511601 s). Three GUI rows/two secondary; no secondary false-alarm qualification. Phase assistance 0. Actual 8,181→51 pairs/all nine LF values; fixture-observed 94,017 conditional +57,267 audit =151,284 jobs, 264 transforms/180 instances. Overrun/drop/interruption/reset zero; original `search_limited=1` remains. 1,016,893 samples, 1,639,494,085-byte peak DSP; 1,824.504075 s process CPU/470.716838562 s fixture wall, not real-time. | `candidate-v5-hf-eight.log`, `candidate-v5-hf-eight-bindings.json`; complete zero-drop/no-cap/no-IO trace `candidate-v5-hf-eight/hf-on-bits-123006646469951.jsonl`, matching tokens. Helper cohort 4,324/7,168 bytes. Wrapper 471.180668950 s; all 441 source inputs/five binaries/fixture/key unchanged before/after. Same reported 36 Hz/interleave 10/crystal/IC-7100/30 MHz/loss 180 dB/data 4 W/helper 1.5 kW, encrypted Auto/rolling/unknown timestamp, controlled CFO 30 Hz/audio 100 ppm/phase diffusion 0.502494 degrees/sqrt(second). Workspace 5,620,189,184/bank 4,212,962,864 bytes; requested +/-90.45 versus represented +/-7.979848 Hz limitation unchanged. No matched HF CPU-gain claim. |
| Prior `f47fa339` HF native/cable helper-on, 900 s controller/910 s PCM | **Limited ordinary GUI completion**: exact source `00101101` on primary ID 2/revision 7, first completed at 395.944593 s and unchanged through 899.961951 s across 20,593 primary observations. Retained pending prefixes include `00` at 66.477 s, `00101` at 149.940 s, `0010110` at 276.171 s. No eight-bit pending poll: `same_pending_completed_row=0`, all-prefix qualification absent. TX passed at 167.371386 s; controller exits 2 because baseline `search_limited=1`, all other health/row flags zero. Runner exits 5: trace reaches 256 MiB cap, 285,529 events dropped/incomplete seal, retaining only a seven-bit primary commit and no eight-bit physical wire/END coordinates. No strict whole-wire/END or work/throughput pass. | `cable/candidate-v5-full-hf8-on-v1-result.json`, SHA-256 `02da9ef36a5957cf551089485f0a68a1387b779521eb802c0ff796525db82e30`, controller/trace/binding. PCM 910 s/48 kHz S16, peak 2,720, zero clips/XRUN warnings. All 441 source, linked/private/key inputs unchanged; children/process groups reaped. Original strict predicates remain unchanged. |
| Prior `a469ce48` HF sampled helper-on, eight bits | **Overall failed, exit 1**, global `per_row_identity=0`: secondary row 3 completed→pending at a later revision during ordinary candidate replacement. Separately, primary row 2/revision 8, wire candidate 2/instance 7 recovered all eight physical-wire and post-XOR source bits exactly, contiguous full wire, exact pending prefixes and ordinary physical END. No overrun/drop/interruption/reset/error; baseline `search_limited=1` remains. 1,016,893 samples; 644.156712181 s fixture wall/3,466.324028 s process CPU, wrapper 644.570289719 s. Fixture reports 8,181→51 pairs retaining all nine LF values, 323,085 jobs = 94,017 conditional +229,068 audits, 267 input transforms/180 instances; phase 0. Peak DSP 1,651,246,806 bytes. | `rolling-qualified-hf-eight.log`, `rolling-qualified-hf-eight-bindings.json`, complete trace `rolling-qualified-hf-eight/hf-on-bits-107855654500403.jsonl`. All source/binary/fixture/key bindings unchanged, child reaped, no external timeout. Primary proof is separate from the failed global gate; no full-HF or matched-speedup claim. |
| Same original HF trace, corrected fixture `558098ca` | **Read-only posthoc passed, exit 0**: all eight primary physical-wire/source bits, exact pending prefixes, primary lifetime identity, revision transitions and every historical completed row's physical END; three rows. Original reception exit 1 is preserved. No new reception, Snapshot source admission or runtime-health check was performed. | `rolling-qualified-hf-eight-posthoc.log`, `rolling-qualified-hf-eight-posthoc-bindings.json`, `rolling-qualified-hf-eight-posthoc-artifacts.json`; original trace SHA-256 `ba32dd85c57dd6e574093282a7ba3904b3325e6550dbaade32a11c9e9a817281`. Corrected fixture binary SHA-256 `b8d87bdef105f25d890aa571a84feb48c1afe661f6289732641ef9a9dc685a17`; focused geometry 1/1 passed 0.72 s. |
| Prior `a469ce48` HF native/cable helper-on, 900 s window | **Strict incomplete, controller/runner exit 2**: full controller history reached 7/8 pending bits `0010110` on row 2 across revisions 2/5/7; no complete wire/source or physical END. This supersedes the initial tail-only five-bit reading. TX passed at 166.937852510 s. Baseline `search_limited=1`; overrun/drop/interruption/reset/error and row-progression flags zero. Frequency narrowing observed; phase inactive. Backlog peaked 835.194935 s/94 banks, peak DSP 1,567,707,093 bytes; no real-time or CPU-gain claim. | `cable/rolling-qualified-full-hf8-on-v1-result.json`, SHA-256 `347e362e71e648902fbd87780e66b0040dc9e8033e6edcedd97f4a3454c751da`, controller/trace/binding. Frozen wrapper `c6ca30e4`; 209,635,716-byte trace complete, 575,286 attempts/zero drops or cap; 910 s/48 kHz S16 PCM peak 2,691, zero clips/XRUN warnings. All 441 source/linked/key inputs unchanged, children reaped. Clean cable is not weak RF. |
| `f49` HF cable preflight v2 | **Exit 2 before TX**, sole health failure `search_limited=1`; overrun/drop/interruption/reset/error 0. Logical 6,120/hardware 48 kHz, source 116.955719 s, initial FFT 45.525 s, helper post ≤ 46.946078 s, planned TX ≤ 166.301961 s. All 441 source/linked/key unchanged, trace complete. Supersedes v1's unlogged-flag uncertainty; v1 retained. No observed helper TX failure. | Frozen harness `0f3de4d4`; `cable/preserved-owner-preflight-hf8-on-v2-result.json`, SHA-256 `a931b6781282f6f89a04a118d9d9d2ca4f877c1dc8d7d326301982290763b6b3`, controller/trace/binding. |
| `f49` HF sampled helper-on, 240 s | **Incomplete exit 2**: 2/8 contiguous primary wire/source bits, pending row 2/revision 2, no END/full-source match; 352,522 samples (57.60 s audio), 240.161199832 wall/325.661186 CPU s. Actual 8,181→51; 9,282 conditional templates/26 transforms; no phase application reported. Baseline search_limited 1, no error/reset/drop, uncapped complete trace. Proves pending ordinary bits/conditional work only. | `preserved-owner-hf-cohort.log`, trace, `preserved-owner-hf-cohort-bindings.json`. |
| Earlier exact HF helper-on, 120 s and 20 s | **Incomplete** 120.178 wall/161.191 CPU s, 0 bits/no END, only 45.709 audio s, insufficient to capture 8×14.222 s plus overhead. Partial 735/8,181 pairs, 4,284 conditional jobs/12 transforms/0 audits; snapshot 0 because cancellation before first barrier. Closing 4,284 includes a started incomplete batch, not completed coverage. Earlier 20 s probe also incomplete. | `hf-eight-integrated.log` and linked trace. |
| Earlier EME helper-on | **Failed ordinary workspace eligibility**, “complete half-chip clock/frequency/rate coverage exceeds DSP workspace.” 2,048 samples/0 jobs/0 bits/no cohort, 16.236 wall/29.534 CPU s; peak 9,057,715,958 versus configured 9,066,749,952 bytes. | `eme-bounded-integrated.log` and trace. |
| Earlier EME helper-off | **Same eligibility failure**, 2,048 samples/0 jobs/0 bits, 15.265 wall/29.381 CPU s. Configured 9,079,320,576 bytes differs from ON 9,066,749,952, so not byte-identical budget or proof helper caused failure. | `eme-off-bounded-integrated.log` and trace. |
| `f49` EME matched 60 s off/on, timestamp 0/test-only 1 bit | **Both incomplete exit 2**: 4,096 samples (0.683 s audio), 29 instances/0 jobs/0 transforms/0 bits each; no error/reset/drop/limit, uncapped complete traces. Same 8,557,590,528 workspace/6,416,013,872 bank bytes. OFF 420.612790 CPU/60.610231328 wall/2,997,319,044 peak DSP; ON 413.275206 CPU/60.304438485 wall/3,000,799,092 peak DSP. Immediate rejection did not recur in this window, but cold preparation preceded scoring; compact backend does not admit common-link helper cohort. | `preserved-owner-eme-admission.log`, traces, `preserved-owner-eme-admission-bindings.json`. Not aggregate acquisition, payload or throughput qualification. |
| Prior `a469ce48` EME helper-on observation | **Cancelled incomplete, managed interrupt exit 130**: last bounded trace sample 43,008/7.168 s audio at trace elapsed 796.889937 s, before helper onset expected no earlier than about 16.7 s audio. Original requested 222,000 samples/3,600 s wall window was not completed. Keyed compact gating prevents cohort creation and queue age prevents the legacy duplicate, so this window cannot exercise the missing conditional path. Raw trace is unsealed/footer absent; no final Snapshot/health, helper measurement/precision, narrowing, payload or CPU-benefit claim. | `rolling-qualified-eme-observation-cancelled-bindings.json`, `rolling-qualified-eme-observation.log`, retained 468,467-byte trace `rolling-qualified-eme-observation/eme-on-bits-109885575140902.jsonl`. Runtime source and all five application binaries unchanged afterward; host process inspection verified zero matching children. Initial sandbox PID lookup found no visible matching processes and sent no signal; managed Ctrl-C cleaned up. Normal wrapper postrun binding was not produced. |

The [GUI contract](gui-contract.md) permits stronger later evidence to reopen a
completed interpretation at a newer revision. The corrected test distinguishes
that allowed secondary-row transition from forbidden same-revision reopening,
prefix/identity regressions and completion without physical END. Its posthoc
trace pass does not replace the original failed run. The earlier `a469ce48` native helper's roughly
+/-0.528 Hz reference radius is an observer qualification bound, not a physical
precision floor: over the 0.2 s phase bridge, frequency uncertainty alone permits
about 0.663 rad, exceeding the 0.25 rad phase gate. Frequency assistance therefore
does not imply independently qualified phase assistance.

In the prior `f47fa339` native HF run, frequency assistance was active. One
coincident record at about 176 s reports absolute helper offset 0.046957 Hz,
reference radius 0.084340 Hz and 86 selected pairs spanning all nine original LF
rates. This single-record selection is not a ratio of unrelated snapshot maxima
or an overall work-gain measurement. Phase assistance never became active;
later canonical-phase-change declines retained the affine convention. Broad
acquisition resumed after reference retirement while the observed eight-bit GUI
interpretation remained stable. The capped trace prevents full wire/END/work
qualification despite this measured frequency-reference behavior.

The EME optional-cache policy (`src/pattern_correlator.cpp` `8f1d056e`, R2 test
`29279e27`) passed private constructor/timing-continuation controls: 55,257
hypothesis fingerprints, effective bit limit ≥ original 36,908, publication 1,024 bits,
reserved workspace ≤ 42,730,143 bytes, mandatory timing/guard/backend fields retained.
These are bounds, not exact new memory points or aggregate success; original
failures remain. See `eme-workspace/r2/observed-results.txt` and run/bindings.

Strong, precise helper assistance for keyed compact EME acquisition remains a
capability gap in the current implementation. The isolated compact conditional
transaction prototype is not integrated or qualified. The existing optional
helper front-end allocation of roughly 7 KiB cannot fund the proposed roughly
6.14 MB shared raw/recovery state. A dense historical helper-replay budget at
159 rows ×55,257 hypotheses would require about 3.80 GB of additional workspace
with the usual timing-entry ABI, versus about 702 MB historical headroom. This
is a sizing estimate, not an observed current allocation or memory-exhaustion
failure. Sparse/rollback certification and a front-end projection mask are under
review to retain the ordinary decoder; no new source decoder is proposed. These
implementation constraints do not establish a permanent physical limit.

Weak-reference precision is separately unresolved: at 205 dB loss predicted helper
C/N0 is about 20.7609 dB-Hz. The prior production observer's recorded minimum confidence
radius, +/-2.2992432 Hz, alone does not establish how many lanes could be removed:
the coincident interval center was not retained. For a zero-centered interval,
any narrowing requires half-width below 1.7390238873 Hz after 0.000977462875 Hz
padding; a +0.58 Hz center shifts the far-edge threshold to about 2.319 Hz.
Roughly one RF lane per LF cell would require about 1 mHz precision while
preserving all five LF cells. These are interval/grid calculations, not measured
qualified narrowing. The reported data link is also about 1.907 dB below its
requested target SNR.

The current v8 observer component completed four deterministic fixed-helper
cases (205/210 dB loss, signal/noise, seed 1): 32 s/768,000 samples at 24 kHz,
CFO +0.58 Hz, 0.0001 ppm and phase diffusion 0.007071 degrees/sqrt(second).
C/N0 was 20.76091259/15.76091259 dB-Hz respectively.

| Loss | Frequency references | Minimum radius, Hz | Maximum error, Hz |
| --- | ---: | ---: | ---: |
| 205 dB | 166 | 2.276992 | 1.649592 |
| 210 dB | 66 | 3.182548 | 3.570028 |

The extrema need not coincide. Both signal cases had zero interval misses,
invalid intervals or date errors, zero phase/timing/metadata references and no
interval at or below 0.25 Hz; each noise-only control published zero references.
`exact-v8-prep/observer-owned/execution-result.json`, SHA-256
`5035cd5a6bc3ccbd3e8584207a088d149876d04bcb58a1ab8cd1d4bf389aed5e`,
binds the current core/source and clean completion (3.7456 s total,
2.2575 s compile/0.7174 s run). It ran alongside normal compilation only;
timings are not throughput/margin evidence. No ordinary payload, full FHSS,
DAC, Live or hardware qualification was exercised. Current Controller EME
geometry has since been recorded as metadata only; ordinary EME reception
remains pending. These are implementation/model precision limits,
not a physical floor or proof of useful ordinary acquisition narrowing.

The earlier accepted observer (`d07290ba`, header `8507dcdd`, core `fbee4890`) and a
private diagnostic variant were compared on one 32 s/24 kHz weak-EME vector,
C/N0 20.7609125906 dB-Hz, CFO +0.58 Hz, seed 1. Both published 165 frequency
references, minimum radius 2.301835304418 Hz, zero intervals at or below 0.25 Hz,
and no phase/timing/metadata references; noise-only controls published none.
Observer CPU was 0.110524 s accepted/0.119169 s diagnostic. This bounded private
result establishes neither finer qualified precision nor ordinary EME search,
payload or CPU benefit. Inputs, summaries and hashes are retained under
`native-precision-private/weak-eme-diagnostic/`, including `results.sha256`.

The directly observed **0/3,565 lanes removed** belongs to the failed private
full-union estimator experiment: a 32 KiB estimator using streaming
payload/encrypted metadata in quantized physical PCM over 35.216 s, with both
original +/-20 Hz and original-domain-prior variants. The latter took 0.519 core
CPU s; inexpensive failure is not assistance benefit. `precision/prior/` remains
unaccepted/unintegrated. These are estimator/model/workspace limits, not a
physical impossibility claim; tone strength, ideal precision or metadata decode
is not EME qualification.

### Framed payload and physical completion

| Binding/case | Actual outcome | Evidence and retained limits |
| --- | --- | --- |
| Current v8 `87513574`, physical-cable 96-byte text | **Strict pass, session/audit exit 0/reaped**: entire 3,648-bit typed wire exact/contiguous/consistent, authenticated 96-byte text/source identity joined through reception ID `1ca4421f26a9291fb87700a8c386cfcc`. Same pending row 2/revision 1, 11,332 pending observations/zero prefix failures; physical END in wire and GUI, all-row END failures 0. Feed 1,004,528 minus physical end 968,120 = 36,408 samples/6 kHz = 6.068 s scored absence. Ordinary encrypted Auto/rolling, existing Complex backend selected automatically, Rate/B 1,500 Hz, SF32, logical 6 kHz/hardware 48 kHz, crystal/IC-7100 at 30 MHz, data/helper 4/1,500 W. Frequency 33→3 and qualified phase/punctuation paths observed; all health/search-limit flags zero. | `cable/candidate-v8-prep/full-long-text96-v1/{execution-result.json,reader-audit.json,observation/}`; hashes above. TX passed at 171.861261 s; authenticated source first observed at 462.300315 s. Filtered trace 40,564,078 bytes, complete/zero drops or cap; typed collector 285,812 observations/156 candidates/failure 0. Maximum backlog **325.855 s: not real-time**. PCM 610 s/48 kHz S16 peak 5,235, zero clips/XRUN warnings. All 441/617/six-artifact guards unchanged, owned children empty. No numeric acquisition-work/CPU gain, weak-RF, phase-accuracy or production-memory admission claim. |
| Current v8 `87513574`, physical-cable 129-byte attachment | **Strict pass, session/audit exit 0/reaped**: entire 4,864-bit dated typed wire exact/contiguous/consistent; authenticated 129-byte file/kind/name `cable-tail-129.bin` joined through reception ID `8d0b134e9e0b8788d8e791ba66b430d3`. Same pending row 2/revision 1, 19,302 pending observations/zero prefix failures; physical END in wire and GUI, all-row END failures 0. Feed 1,316,336 minus physical end 1,279,932 = 36,404 samples/6 kHz = 6.067333 s fully scored absence. Same ordinary encrypted Auto/rolling profile and automatically selected existing Complex backend as text96; qualified frequency/phase/punctuation timing observed, all health/search-limit flags zero. | `cable/candidate-v8-prep/full-attachment129-v1/{execution-result.json,reader-audit.json,observation/}`; hashes above. Source duration 212.578833 s/TX passed at 223.967956 s. Original 900 s Controller/910 s recording windows retained; session 911.129494 s/reader 7.733596 s. Filtered trace 77,266,951 bytes, complete/204,689 attempted/zero drops or cap. Maximum backlog **607.766333 s: not real-time**. PCM exactly 910 s/48 kHz S16 peak 5,235, zero clips/XRUN warnings. All 441/617/six-artifact guards unchanged, owned children empty. No numeric acquisition-work/CPU gain, weak-RF, phase-accuracy or production-memory admission claim. |
| Prior `f47fa339` native long-source preflights | **Both passed, exit 0/reaped**, ordinary encrypted Auto/rolling, receive timestamp zero, Fs 6 kHz/B 1,500 Hz/SF 32/symbol 256 samples. Planned text 96 bytes/3,648 wire bits, source duration 160.696167 s/TX maximum 170.533833 s; attachment 129 bytes/4,864 wire bits, 212.578833 s/TX maximum 222.416500 s. Zero health/search-limit flags, complete zero-drop traces and unchanged source/linked/key inputs. These preflights requested no TX and establish no payload reception or CPU gain. | `cable/candidate-v5-long-private/candidate-v5-preflight-{text96,attachment129}-v1/{result.json,binding.json}`. Test observer uses a fixed 32 MiB data budget plus fixed metadata. Both full cases passed below. |
| Prior `f47fa339`, native/cable 96-byte text | **Strict pass, controller/runner/recorder exit 0/reaped**: entire 3,648-bit typed physical wire exact/contiguous, authenticated 96-byte text and source identity joined through reception ID `3bcbff3acbdb96f370aa28be78a6a746`. Same pending GUI row 2/revision 1, 11,510 retained observations/zero prefix failures, ordinary physical END: feed 1,002,992 minus end 966,688 = 36,304 samples/6 kHz = 6.050667 s scored absence. Actual TX epoch 1,792,893,714; primary helper acquisition and qualified frequency/phase paths observed. All health/search-limit flags zero. | `cable/candidate-v5-long-private/candidate-v5-full-text96-v1/{result.json,binding.json,controller.jsonl,wire.jsonl}`. Light trace 72,777,421 bytes, complete/194,317 attempts/zero drops or cap. Typed collector 285,812 observations/156 candidates/failure 0, fixed 32 MiB data +262,144-byte metadata. Peak DSP 1,366,698,400 bytes/backlog 333.092333 s/172 banks. PCM 610 s/48 kHz S16 peak 5,229, zero clips/XRUN warnings; source/linked/private/key unchanged and cleanup complete. No numeric acquisition-work, real-time, RF weakness or phase-accuracy claim. |
| Prior `f47fa339`, native/cable 129-byte attachment | **Strict pass, controller/runner/recorder exit 0/reaped**: entire 4,864-bit typed wire exact/contiguous/consistent beyond the 4,096-bit preview; exact authenticated 129-byte file/kind/name `cable-tail-129.bin` joined through reception ID `70bc642944be557849828f17770eb2b6`. Same pending/completed row 2/revision 1, 19,575 retained observations/zero prefix failures. Physical END: feed 1,312,240 minus end 1,276,004 = 36,236 samples/6 kHz = 6.039333 s scored absence. Actual TX epoch 1,792,894,410; primary candidate 1/instance 13 helper-acquired/assisted; qualified frequency/phase paths and later bounded phase fallback observed. Ordinary Auto/rolling RF/LF policies retained; all health/search-limit flags zero. | `cable/candidate-v5-long-private/candidate-v5-full-attachment129-v1/{result.json,binding.json,controller.jsonl,wire.jsonl}`. Result SHA-256 `02000bc23f7fc7567948b0c9efdf526ee11bdf7fd043bbf1c51ddfad9e63e958`. Light trace 22,993,929 bytes, complete/57,222 attempts/zero drops or cap; typed collector 507,260 observations/208 candidates/failure 0, fixed 32 MiB data +262,144-byte metadata. Peak DSP 1,333,250,607 bytes/backlog 619.624 s/224 banks. PCM 910 s/48 kHz S16 peak 5,232, zero clips/XRUN warnings; all 441 source/linked/private/key inputs unchanged, children reaped. No numeric acquisition-work, real-time, RF weakness or phase-accuracy claim. |
| Integrated guard, matched ordinary rolling payloads | **Passed** helper-off/on: 18-byte framed text/entire 1,216-bit wire in 47.12 s, and 17-byte attachment/entire 2,432-bit wire in 170.22 s. Exact physical wire, authenticated source kind/filename/data, pending row through physical END; legitimate TX epochs 1,800,000,000 off/1,800,000,005 on. No work or cable-throughput gain claim. | `continuation-guard-focused-ctest.log`, `helper_payload_tail_framed_text` and `helper_payload_tail_attachment`; whole ten-test group remains failed on the separate Live fixture assertion. |
| Earlier lead/tail regression | Initial helper 51/64 versus OFF 64/64 raw bits **failed**; rescheduling restored 64/64. Framed text authenticated source but helper 1,207/1,216 raw bits **failed**. Settling-prefix correction then passed both 1,216 actual wire bits/authenticated 18-byte source/pending identity/ordinary END in 46.99 s. Earlier attachment 2,432 bits passed 143.92 s before later phase/staging integration. | `payload-text-prefix-corrected.log`, `payload-tail/text-prefix-corrected-last-test.log`, `payload-framed-rescheduled.log` (text failure retained with attachment pass). These codepoints do not qualify final long payloads. |
| `a469ce48`, simulated helper-on same 257-byte text | **Passed `decoded_exact`, exit 0**: entire 7,296-bit dated physical wire and authenticated source exact; contiguous full wire, all prefixes, pending row 2/revision 1 through ordinary scored END, later epochs and actual helper acquisition/phase work. TX epoch 1,800,000,005; 20 rows/19 secondary, all completed rows scored END. No health flags. 1,973,187 samples, 1,764.932775471 s wall/3,087.700406 s process CPU, peak DSP 1,488,654,796 bytes. | `rolling-qualified-long-text.log`, `rolling-qualified-long-text-bindings.json`, sealed trace in `rolling-qualified-long-text/`; fixture `10fe3bea`. Full source kind/filename/data equality and authentication are checked after stream completion, beyond the 4,096-bit diagnostic prefix; typed observer verifies the entire physical wire. Secondary rows remain separate and do not establish false-alarm qualification. |
| `a469ce48`, native/cable helper-on 96-byte text | **Strict pass, controller/runner/recorder exit 0**: entire 3,648-bit physical wire exact; authenticated 96-byte source kind/filename/data matched through reception ID `91581fbd5377ba3001eef99d699f3135`. Same pending row 2, 11,665 GUI observations/zero prefix failures, no health/errors. Ordinary END feed 1,002,480 minus waveform end 966,120 = 36,360 samples/6 kHz = 6.06 s scored absence. Native epoch 1,792,878,545; helper acquisition/narrowing and qualified phase paths observed. | `cable/long-source-private/rolling-qualified-full-text96-v2/{result.json,binding.json,controller.jsonl,wire.jsonl}`. Result SHA-256 `b4c437cfda5ece1aba4f86202947a2742933df4c744144bc71c22769bf0c7695`; resources and scope below. Earlier `native-seed-full-text96-v1` incomplete result remains unchanged. |
| `a469ce48`, native/cable helper-on 129-byte attachment | **Strict pass, controller/runner/recorder exit 0**: entire 4,864-bit wire and authenticated file `cable-tail-129.bin` exact through reception ID `49189c45050307e2279743330a481b03`, beyond the 4,096-bit diagnostic prefix. Same pending row 2, 20,089 GUI observations/zero prefix failures, no health/errors. Ordinary END feed 1,311,216 minus waveform end 1,274,908 = 36,308 samples/6 kHz = 6.051333 s scored absence. Native epoch 1,792,879,221; helper acquisition/narrowing and phase paths observed. | `cable/long-source-private/rolling-qualified-full-attachment129-v2/{result.json,binding.json,controller.jsonl,wire.jsonl}`. Result SHA-256 `4d3d6b844f1cadbac8ec319c64fcb8f14d232a85b12f0c18d5cd99a4cfcbfde5`; resources and scope below. |
| `f49a3b2a`, simulated helper-on 257-byte text | **Failed runtime exit 1**: 2,914/7,296 pending bits, 279.324330207 wall/506.108053 CPU s; input_interrupted 1, “simulation receive staging exceeds the DSP event capacity.” | `long-auto-text.log`, `long-auto-text/`, fixture `81caec8e`; 109,938,582-byte trace complete, neither cancellation nor trace cap. `long-auto-text-instrumentation-limit.json` preserves corrected preliminary classification. |
| `965dd7b3`, same simulated text | **Incomplete exit 2**: 5,397/7,296 contiguous primary wire/source bits, pending row 2/revision 1, no END; 900.227397514 wall/1,597.066122 CPU s, 1,425,971 samples, 15 rows/14 secondary. No runtime/health error; staging refusal did not recur in this window. | `timeline-owner-long-text.log`, `timeline-owner-long-text-bindings.json`, fixture `b339e5e6`; collector/trace complete, 0 drops. Private staging repair is not full payload completion. |
| `56c93b10`, same simulated text | **Incomplete exit 2**: 6,831/7,296 contiguous primary wire/source bits, same pending row 2/revision 1, candidate 1/epoch 1,800,000,005, no END/authentication/full-wire match; 1,800.320910789 wall/5,565.228149 CPU s, 1,792,101 samples (298.68 s audio), 16 rows/15 secondary, peak DSP 1,442,785,455 bytes. No runtime/health error or staging refusal in window. | `native-seed-long-text.log`, `native-seed-long-text-bindings.json`, fixture `2bd37c5f`; collector/trace complete, 0 drops. |
| `56c93b10`, native/cable helper-on 96-byte text | **Incomplete 360 s functional window**: 3,195/3,648 observed bits on pending GUI row 2/revision 1; contiguous/consistent wire candidate 1 at independently observed epoch 1,792,874,254, no END. TX passed 171.567294570 s; no authenticated source or matched primary/source identity, controller 3/runner 5. No health/error/GUI-prefix-violation flags. **Separate diagnostic overflow**: 268,434,588-byte trace, 3,108,285 attempted/2,567,167 dropped records, incomplete seal. No attachment launched. | `cable/long-source-private/native-seed-full-text96-v1/{result.json,binding.json,controller.jsonl,wire.jsonl}`. Typed collector failure 0 is not exact source qualification; details below. |
| `f49`, fast-auto 257-byte attachment baseline | **Cancelled incomplete**, reaped 130 before 900 s cap, no ON leg. At~600 wall/~150 media s, 2,361 primary bits pending. 8,512 wire bits require about 544.7 s payload media plus at least six seconds of scored absence. | `fast-auto-attachment-cancelled.json` and retained trace; not functional failure/performance result. |
| `f49`, long-auto helper-on 257-byte attachment | **Cancelled incomplete evidence**, reaped 130 at about 120 wall/~85 audio s before 8,512 wire bits completed. Verbose per-row trace reached unchanged 256 MiB cap, 268,434,359 bytes. | `long-auto-attachment-trace-limit.json`, log/trace; no payload pass/fail/gain. Lighter-trace rerun pending. |
| Wider candidate gates | 12 kHz full-support FIR **failed**, 0.0015198 amplitude bound versus 0.001. 8 kHz produced metadata only, no full resource/acquisition qualification. | `wide-auto-gate-{off,on}.log`, `wide-auto-8k-gate-{off,on}.log`. |

The historical `a469ce48` simulated 257-byte/7,296-wire-bit text pass used ordinary
encrypted Auto/rolling, profile L geometry,
receive timestamp zero, 30 Hz CFO, 100 ppm clock error, 0.502494 degrees/sqrt(second)
phase diffusion and unchanged 35.249387 dB payload SNR. Resolved workspace
was 5,805,236,224 bytes/bank 4,351,748,144 bytes; measured support narrowed 33→3
pairs and actual helper resources used 4,940 of 7,168 reserved bytes. That historical
`a469ce48` collector sealed complete with failure `none`: 1,138,953 observations/312 candidates,
1,602,816-byte peak data capacity plus 458,752-byte fixed metadata. The complete
67,593,619-byte trace has matching begin/end tokens, 91,686 attempts, zero drops
and no cap/IO failure; SHA-256
`f9abc5ef55716f2e12fcc97d72dfe353d5e04980abfa7eeedaf7220f82b867ca`.
Wrapper bindings retain all 441 source inputs, five binaries, fixture and keyfile
unchanged before/after, with child reaped and no external timeout. This helper-on
functional pass is not a matched CPU gain or phase-accuracy measurement. The
separate historical `f47fa339` 1,024-bit pair supplies the comparison above.

The earlier simulated text collectors were complete, bounded by 32 MiB data plus 458,752 bytes
fixed candidate metadata: `965` 623,596 observations/231 candidates, 939,264-byte peak;
`56c` 966,137/240, 1,390,592-byte peak. These are diagnostic capacities, not DSP grants.
`56c` snapshot maxima 8,143,260 conditional/44,683,848 audit jobs and 0 selected pairs
at end are not full-instance work totals or a gain measurement. Late broad fallback
remains under investigation without an established cause.

The prior `a469ce48` native 96-byte run retained a complete 80,465,965-byte trace,
216,275 attempts/zero dropped records and no cap/IO failure. Typed collector:
285,812 observations/156 candidates, failure 0. Peak DSP was 1,406,899,809 bytes,
with maximum backlog 340.952 s/172 banks: this is eventual functional completion,
not real-time throughput or CPU-gain qualification. The 610 s/48 kHz S16 recording
peaked 5,229 with no clipped samples, recorder-loss warning or xrun; recorder log
was empty. All 441 source inputs, linked binary/private inputs and keyfile hashes
were unchanged, and owned children were reaped. Capture coverage uses process/GUI
intervals without hardware-frame timestamp alignment; phase accuracy and weak-RF
performance remain unqualified.

The prior `a469ce48` native attachment run retained a complete 30,188,887-byte trace,
77,155 attempts/zero drops and no cap/IO failure. Typed collector: 507,260
observations/208 candidates, failure 0. Peak DSP was 1,423,586,945 bytes, with
maximum backlog 642.013667 s/224 banks; no real-time or CPU-gain claim follows.
The 910 s/48 kHz S16 recording peaked 5,228 with zero clips, recorder warnings or
xruns. Source/linked/private/keyfile bindings remained unchanged and all owned
children were reaped; capture timing has the same process/GUI-only alignment
limit as the text case.

The initial automatic approval request for the preflight timed out before launch. One explicitly
authorized identical retry passed without requesting TX; this resolved tool setup,
not a runtime failure. Its evidence is
`cable/long-source-private/rolling-qualified-preflight-text96-v2/result.json`.

The earlier native 96-byte run had peak backlog 217.637333 s/143 banks, observed narrowing and
qualified phase paths, but `primary_row_id=0`, `entire_wire_exact=false`,
`wire_qualification_pass=false`: zero recorded prefix violations is not exact-source
success. Typed collector 219,382 observations/137 candidates, failure 0, despite separate
trace overflow. Source/linked/keyfile checks unchanged and cleanup complete;
`result.json` binds controller/logs/wire/capture. Its 370 s/48 kHz S16 recording
peaked 14,986, no clips. Observed process/GUI intervals cover TX without hardware-frame
alignment. This is clean-cable diagnostic evidence, not weak-RF/phase-accuracy/CPU gain.

### Remaining acceptance

The prior `a469ce48` 257-byte text pass closes the reproduced staging/full-7,296-bit
functional regression with authentication/decompression and scored physical END.
The original failure and 900/1,800 s attempts retain their failed/incomplete
statuses and are not elapsed-time baselines. The test-only manual allowance did
not change CTest defaults, deadlines, assertions or receiver length knowledge.
Other payload/attachment configurations and broader native/RF conditions still
need full wire/progress/END and resource/phase evidence; the passing 96-byte
text/129-byte attachment cable cases do not substitute for those checks.

The prior complete normal `e2b951fc` matrix, including registered preservation,
shared FLTK GUI, audio/browser and calibration checks, passed 379/379 with no
omissions. Its coherent application binding and explicit runtime-binary
equivalence to `f47fa339` measurements are recorded above. The original normal
run failed/interrupted and remains preserved. Complete HF requested coverage,
useful weak-EME reference precision, broader cable/RF conditions, full sanitizer
and desktop native/Rev qualification remain outstanding. The instrumented build
passed; sanitizer Core/Fast/Live/Preparation/Calibration failed with eleven
original failures above, including the Fast empty-output UBSan finding.
Frontends/Web passed; final inventory/binding/cleanup audit passed separately.
Those prior results did not include native GUI/Rev. Separate e2 cover-tail/helper-workload/private-GUI
diagnostics and the standard Live control passed; unchanged GUI/Live selectors
failed. None clears the eleven original failed qualification results. The RF/
clock-cell certificate, calibrated placement fallback and Fast empty-output
guard were integrated in v7. Its 15 selected normal tests passed, but fresh-five
normal failed 378/379 and serial Core failed 323/325; the standalone selector
pass and conditional practical long-scope reuse clear neither failure.
Current v8 lifecycle/payload focus, the exact 1,024-bit pair and reviewed work
audit r1 passed; the original failed audit remains preserved. Measured gains
retain the supported profile's limits above. Current named sanitizer focus and
three direct gap functions passed within their selected scopes; the full
seven-scope normal matrix passed all 381 freshly selected cases, with no
omissions. Native FLTK build and its three complete native suites passed.
Current HF8 simulation passed within its limited domain; EME geometry
is metadata only. Current cable 96-byte text and 129-byte attachment passed their
strict functional scopes, with maximum backlogs 325.855/607.766333 s and no
real-time/work-gain claim. Current HF cable finished limited/failed despite exact
completed-wire/END diagnostics: strict full-source pending gates and baseline
coverage remain unqualified. Longer payloads, full sanitizer,
full-requested-coverage HF and ordinary EME qualification remain pending. Prior
matrix/performance/hardware outcomes and conditional v7 reuse provide no
equivalence or full-coverage bridge to v8.
The earlier `a469ce48` cable and payload evidence retains its prior binding.
The same-primary extension remains a preserved failed witness; the integrated
guard passed maintained cover-tail, selected matched payload/loss checks and the
repaired complete Live fixture. Original failed runs retain their outcomes.
HF native/full requested coverage, EME, desktop native/Rev and sanitizer qualification remain pending; prior
native text/attachment passed their stated strict scopes, while HF GUI completion has the strict observation/trace
limits above. Strong-reference compact
helper assistance is also unfinished. The small header/probe
sanitizer check is separate. Historical v4/v3 results bind their own inputs and
never qualify a newer runtime merely through chronology. These results do not
establish task completion, a full multi-profile/platform matrix, broad hardware/RF
qualification, full sanitizer coverage or weak-link qualification.

## Prior Helper Carrier v3 candidate — superseded architecture

All results in this section bind the earlier v3 source and binaries. Its
fixed-timestamp/Complex-only policy is superseded by the common-link rolling
architecture above. Its original failures, incomplete coverage and measured
hardware results remain historical evidence, not current qualification.

The previously accepted source candidate was based on
`b2d3539919f0955cee94986269ccbd6402e37b1b`, with 42 application/test/build inputs
and overlay SHA-256
`107aa9d0ad36910fc087015b6357b006a412552092473a81560012f8086606ff`.
`local-evidence/helper-v3/final/source-inputs.json` binds those inputs;
documentation changes are excluded. Source review and the focused containment
checks below are accepted. The normal application/GUI/worker build passed for
this source, and the focused ASan/UBSan and shared GUI checks below passed. The
294-registration normal run completed with 288 passes, five setup failures and
one timeout. Exact web and native display retries passed all five setup-failed
cases with unchanged source and binaries, giving 293/294 distinct cases passed.
The current physical check observed clean transmission and same-row pending
`001` to completion, but strict individual-prefix coverage remains incomplete.
Contract preparation coverage is **failed/incomplete**:
`scheduled_parallel_preparation` timed out at 1,500.12 s.
No full matrix pass is claimed.
New binary identities are recorded in
`local-evidence/helper-v3/final/containment-application-build-result.json` and
`normal-binary-inputs.json` in the same directory; earlier binaries are not
provenance for this source. No publication or certification is claimed.

Earlier diagnostics retain their own source identities. The earlier 33-input
overlay is
`80ac7f2fba143fca05d623c6cb8cd2e5e94158a7561abb7d723390c1638d205e`, retained
in `local-evidence/helper-v3/final/pre-bootstrap-source-inputs.json`. The later
overlay is `22125d5bbf5344afa5cf11a243ba125513706df48ffde6648623544521b11027`;
its source and linked runtime identities are preserved in
`local-evidence/helper-v3/tx/receive-22125-build-binding.json`. The earlier normal
binary identities remain in `final/pre-bootstrap-normal-binary-inputs.json`
under the same evidence root. Documentation and ignored evidence are excluded
from the source overlays. These historical bindings remain separate from the
current 42-input manifest. A subsequent normal placement build and repaired
test-only fixture are identified separately below.

Neither historical diagnostic snapshot is a qualified final source. The later
HF diagnostic recovered the expected ordinary message, but receiver-health and
separately observed bit-prefix checks remain unsatisfied; the EME run was
cancelled incomplete. Full qualification of the current candidate remains
pending. Those snapshots predate
the fixed-epoch containment policy below; they do not establish final rolling-
reception assistance. No publication, promotion or certification is claimed.

Messages travel exclusively on ordinary Robust signals. Helper carries only
its tone, punctuation and optional callsign/time. Actual-length callsigns use
one to seven APSK symbols; at most 48 Q16 Unix-time bits fit three symbols/24 ms.
FF1/AES-256 encryption preserves length without an authentication tag, and CRC7
uses only existing spare bits. Metadata remains unauthenticated and grants no
UTC or payload authority. Keyed operation hides and ignores helper callsign,
even invalid inactive preference text; clear mode validates it. This
normalization/parser change passed source review and the focused shared
controller/parser checks below. Helper-only transmission remains a Developer action. See the
[reference](helper-carrier.md) and [plan](helper-carrier-plan.md).

### V3 behavior and fixed-timestamp containment policy — superseded

Helper Power remains editable, with 10 kW and 15 kW presets added. High/Low
placement uses actual shaped occupied data bandwidth plus gating allowance and
the nearest power-qualified leakage guard. The former logical-sample-rate High
floor is removed. The startup failure, “Helper Carrier requires a valid data
passband at the output sample rate,” came from treating the default shaped
3.6 kHz setting as an unshaped 3.6 kHz band. The corrected occupied bandwidth
fits its existing data carrier without changing Rate. Narrow 100 Hz, SSB
3.6 kHz and wide 12 kHz settings all retain selected modem geometry; there is
no automatic Rate adjustment. Hardware FHSS output preparation also no longer
adds a duplicate 200 Hz gate allowance; the reported EME configuration now
plans its High helper at 2,250.3125 Hz. This is a placement correction, not a
passing EME reception result. Closest digital placement still does not promise
that a radio's narrow SSB filter can pass the complete data/guard/helper bands.

Mixed Live output can disable an unavailable helper and visibly report the
reason while retaining ordinary unscaled payload PCM. A helper-specific device
setup request can retry once without that request only before the first PCM
callback. An actual output gap still fails TX without retry. Helper-only output
fails when its helper cannot be generated. Hardware-rate negotiation remains
independent of the logical modem clock, with a practical 48 kHz helper floor.

The decided primary-replacement policy is restricted to a fixed configured
transmit timestamp with an unchanging epoch-admission bank, using the same
fixed-bank condition as ordinary refresh. It also requires explicit
**Complex only**, an actual native continuous/noncalendar/noncompact backend and a
structurally identical bounded normal `Error`-reset recipe. Clock sync, FHSS,
DSSS/Interleave, calendar, compact, configured drift and differential paths are
excluded from primary replacement. Auto, energy,
differential, compact/correlator, scheduled and rolling/unknown-epoch cases
retain their ordinary bank; an additional helper lane is separately eligible. Eligible large fixed banks
can use at most 257 frequency/clock alternatives per narrowed profile while
retaining all admitted key/profile/private-epoch and original start alternatives. The original clock
domain includes continuous residual uncertainty in helper-to-data transfer;
only an equivalent quarter-chip-sized domain can share a clock hypothesis.
Before qualification, bounded history attempts to retain the full original
initial acquisition aperture. Switching declines if history starts after an
original receiver origin. Precommit replay eligibility bounds the configured
physical capture aperture and its resources; it does not use received length or
a message-size quota. Full configured source quotas remain intact. Exact dormant
ordinary state and subsequent raw PCM remain available for replay until the first
bit accepted by an ordinary detector. Reference loss, failed acquisition or unavailable bounds before that
boundary restore the original state and input. The first ordinary accepted bit
commits that same full decoder and releases fallback state; the precommit bound
no longer limits accepted payload lifetime. No helper-specific message-length or
source quota applies. Accepted payloads retain ordinary limits and future
acquisition can broaden on helper loss. The additional small optional lane
remains separate. Native-backend containment and immutable cold-start/replay
grants for ordinary `Error` recovery passed source review and focused checks;
the focused sanitizer run passed, while full contract coverage remains
incomplete and broader sanitizer qualification remains pending. Both retain full long-text
and attachment framing, source quota, decompression, recovery, next-poll bit
progress and six seconds of fully scored absence. Tone/metadata never commit
payload or supply END.

Adjacent assisted reception can use a real linear bandstop FIR; its fully
observed centered windows retain the actual input offset and never insert EOF
zeros. Dormant original receivers and FIR-delayed consumers remain in processing
frontier accounting. Raw fallback is unchanged. Filtered noise is correlated;
the helper presence budget does not certify payload false-alarm behavior after
this frontend. Link Planner shows prospective conditional counts and their
required measured frequency uncertainty. Its ordinary payload SNR, probability
and CPU curves remain unchanged; those counts are not a measured speedup.

Weak-frequency existence admission combines independent fixed-grid raw-time
evidence groups with the single-window/variance allowances under the stated
AWGN model. Refined covariance/model intervals are local estimates, not exact
global 99% confidence intervals. Centered-CW bias cancellation is restricted to
a timing-qualified straight dwell region; unqualified windows that could cross
punctuation or metadata retain the conservative model bound. No uniform weak
qualification or GPSDO replacement is established. The metadata correction
projects the reversal through the actual sampled real-carrier/finite-Gram bin
model instead of a scalar envelope average, removing a carrier-phase-dependent
timing bias in clean dense metadata fits.

### Completed focused normal checks preceding the later diagnostics

Checks used the compatible `build/dev` Release GCC 14/FLTK/system OpenSSL profile
with ordered floating point and unchanged resource margins. The first nine-helper
run passed six and failed three in 127.09 seconds. Its failures are retained:
`helper_live` did not decode a strong callsign, `helper_receiver` failed its
original assertion, and `helper_sim` failed its near-Nyquist passband assertion. The
subsequent corrections and focused reruns are separate results:

| Scope | Completed result |
| --- | --- |
| `helper_live_assistance` | Passed in 109.36 s in the nine-helper run, including ordinary full-payload and fallback cases. |
| `helper_output_fallback` | Passed in 0.03 s; mock-provider checks include unchanged ordinary PCM and no retry after output begins. |
| `helper_conditioned_native` | Passed in 4.62 s; deterministic native capture/processing-frontier fixture, not physical audio hardware. |
| `helper_carrier`, `helper_metadata`, `helper_assistance` | Passed in 0.14/0.01/1.31 s respectively in the nine-helper run. |
| Corrected metadata/Live reception | `helper_live` and `helper_receiver` passed in 15.55/1.89 s, 17.45 s total. |
| Conservative receiver and simulation controls | `helper_receiver` and `helper_sim` passed 2/2 in 4.05 s. |
| Focused Link Planner helper checks | Passed after correcting the floating-point preset fixture; selected 100 Hz, 3.6 kHz and 12 kHz geometry remains unchanged. |
| CLI | Initial run passed 45/46 methods in 47.754 s. The 19-byte framed helper fixture incorrectly expected unrestricted `!` instead of the received-text replacement `_`; the corrected fixture passed separately in 3.692 s. Application source was unchanged, so the other 45 method results remain applicable; this was not a fresh complete 46-method pass. |

Detailed evidence is retained in
`local-evidence/helper-v3/final/helper-focused.log`,
`helper-focused-details.log`, `conservative-focused.log`,
`local-evidence/helper-v3/metadata-review/focused-final.log`,
`local-evidence/helper-v3/planner-tests-diagnostic.log`,
`planner-tests-corrected.log`, `local-evidence/helper-cli-full-v3.log` and
`local-evidence/helper-cli-framed-v3-corrected.log`. Original failures remain
failures rather than being relabeled by the passing reruns. These focused
results do not constitute a full normal regression pass or a new CPU benchmark.

### Accepted containment and focused evidence

The accepted containment inputs include `src/live.cpp` SHA-256
`6db6403ff86b4edfe87a0aaf9aae5d70aeb330db0f97457d751e51d38f442153`,
`src/pattern_receiver.cpp`
`4503fe72fb0ef0be4617c50d4a660016026c1d1f5a5810ff3b4a162b54e2665d` and
`src/streaming_modem.cpp`
`3ad9451439200133dc869e4df370a4ad009c76005a45753104270ceabc1c87ef`.
Focused fixtures retained their full configured 1 MiB source quotas. The
precommit resource boundary did not become a message-size or received-length
quota.

| Focused scope | Accepted result and limit |
| --- | --- |
| Finite resource query | Native and compact queries cover all 32 ordinary source candidates and the 111/112 aperture boundary. This is resource-query geometry, not a payload-length cap; compact primary replacement remains ineligible. |
| Fixed-bank exact bits | Exact `001` and scored END across all five original epochs; 4,625 oscillator alternatives became 45. |
| Full framed source | The 19-byte/1,216-bit fixture retained FEC, decompression, recovery, bit progress and scored END: 42.586 s process CPU for 790.769 s media. |
| Native-provider paths | Conditioned success and exact raw fallback passed. These use actual 48 kHz PCM through a provider fixture, not another physical-radio qualification. |
| Ordinary error recovery | Five forced ordinary-row `Error` resets retained immutable cold grant 769,730 and replay grant 1,164,226 bytes without an input-domain/provider/revision reset or drops. Later negligible-helper `001` and scored END passed over 17.28 s media. |
| Rolling admission | Primary replacement declined; helper off/on constructor grants were equal and both recovered exact `001`. CPU totals were 231.643/221.541 s; no rolling speedup is claimed. |
| Fixed strong-reference comparison | Matched ordinary control used 240.128 s process CPU versus 0.678877 s with assistance for 14.5552 s media, about 354× in this synthetic strong-reference fixture only. It does not qualify weak HF/EME reception, observer cost or general real-time CPU margin. |

Evidence is retained in `local-evidence/helper-v3/conditioning-two-grant-*`
logs, including unit2, 001, full, error3, rolling, control, native-success and
native-fallback. Earlier native-only declines and a 90 s full-framed timeout
remain retained. A compact fixture with two tracks exhausted its output queue;
the corrected fixture uses the ordinary default of 16. A bright-helper
post-error control also failed its 90 s bound. A repeated trace-path setup
failure was followed by a passing fresh-directory run; this sequence does not
establish a single cause for every earlier failure. The native fixture's unique
temporary filename changed only after these passing runs; the new normal
application build passed, but its default regression run still must complete. None of those failures is relabeled as
a pass by the accepted focused checks.

### Focused ASan/UBSan — selected paths passed

The focused run passed all 41 commands: 11 fresh instrumented compiles,
12 instrumentation checks, eight links and ten runtime commands. Twelve actual
implementations were instrumented; metadata alone reused an object after exact
source/include-closure and object-hash verification. All 219 frozen input
hashes and the current 42-input overlay matched. Every command exited 0 and
every direct child was reaped, with no timeout, retry, ASan or UBSan finding.
Scoped process verification found no remaining owned process and released the
CPU window.

Coverage includes the actual helper DSP, channel, Pattern/Streaming/Correlator
receivers, framed transfer and FIR input-origin/EOF handling, plus native-provider
three-bit Live success, original replay and ordinary-Error replay. The framed
batch check recovered the exact 19-byte/1,216-bit source and withheld completion
when six seconds were removed from the observed quiet tail. The three native
selectors used requested and observed CPUs 0–7 with unchanged 90 s internal and
300 s outer limits; other commands used CPU 0. Compilers ran sequentially on one
CPU under compiler-only 600 s/5 GiB limits. These are functional checks, not
throughput measurements.

Leak detection was disabled (`detect_leaks=0`). Full-payload Live, GUI and
production CLI entry points, physical devices/RF, throughput, the platform
matrix and full sanitizer/release qualification are outside this scope. Normal
archive dependencies, OpenSSL, LZMA and system libraries were not instrumented.
Exact commands, hashes, results and process limits are retained in
`local-evidence/helper-recenter/sanitizer/result.json`, `report.md`,
`identity.json`, `commands.jsonl` and `process-verification.json`.

### Focused shared GUI/controller/parser checks — passed

The focused GUI targets built successfully for the unchanged 42-input source.
All three runtime checks passed: controller `--helper-controls` in 0.4146 s,
the complete launch-command parser suite in 0.0081 s and planner `--helper-only`
in 0.2644 s. Coverage includes invalid inactive encrypted callsigns with clear-
mode recovery and Live validation, explicit strategy/fixed-versus-rolling gates,
unchanged Rate and the reported HF/EME planner counts. It does not establish
full shared GUI or native-adapter conformance. Source binding, commands and
results are in `local-evidence/helper-v3/final/containment-focused-gui-result.json`
and `containment-gui-*.log` in the same directory.

### Later placement build and hardware-rate output fixture

The normal application/GUI/worker build passed for 33-input overlay
`39954499171f16b28325ef6c26eb30fd06688183b9686f6f0751eb5165bc5b2b`, recorded
in `local-evidence/helper-v3/final/placement-source-inputs.json`, with build and
binary evidence in `placement-application-build.log` and
`placement-normal-binary-inputs.json`. This application build is not full
regression qualification. Subsequent repairs changed only the new test fixture;
the final `tests/test_helper_output_fallback.cpp` input SHA-256 was
`67a2dcbe412707ebdb9f96dd79313d82d83dbc1c8af4b74ea88471621d084430`.

The complete fallback suite, including the FHSS emitted-frequency check on actual
48 kHz output PCM through the mock provider, passed in 0.683 s. This checks the
output waveform against the common single-gate placement, alongside exact
ordinary fallback PCM, output discontinuity handling and helper-only failure.
It is mocked-provider PCM coverage, not an additional physical radio test.
The passing output is retained in
`local-evidence/helper-v3/final/placement-output-utc-test.log`.

The original fixture first failed its gate-allowance assertion before playback.
After that setup repair, a mock UTC-acceptance timeout reported one open, one
format callback and zero source callbacks (`1/1/0`). A matched fixture with
proper mock UTC acceptance passed. Those are test setup repairs, not production
TX failures. The failed checks and matched control remain in
`placement-output-test.log`, `placement-output-repaired-test.log`,
`placement-utc-before.log` and `placement-utc-after.log` in the same evidence
directory; they are not overwritten by the passing run.

### Earlier exact HF simulation — no payload acquisition

The actual-controller simulation of the reported encrypted crystal/IC-7100 HF
configuration, with 1.5 kW helper, 4 W data and ordinary raw bits `001`, completed
its bounded probe with exit 3 after 600.378 s wall time. The process was reaped;
this is an unsuccessful acquisition result, not a passing transmission or
physical-end test. Helper pending and received rows were observed, but no
ordinary bits or payload END were observed. No meaningful assistance benefit
for this exact case is demonstrated by the earlier `80ac7f2f` overlay.

Resolved geometry was 6,120 Hz logical DSP, 14.2222 s per payload symbol,
100 ppm sample-clock error, 30 Hz carrier offset and
0.50249 degrees/sqrt(second) phase diffusion. Estimated helper C/N0 was about
39.7 dB-Hz after the existing interleaved waveform backoff; this is a configured
budget, not measured precision. The ordinary search reported +/-7.97985 Hz
coverage against +/-90.45 Hz requested. At the deadline the probe reported
45.646 s of media, zero committed processing samples, 59 receiver instances,
1,470,007,381 bytes requested DSP buffering and 3,978,260 KiB process peak RSS.
The final reported aggregate process CPU time was 4,774.59 s. These diagnostic
resource totals do not qualify real-time margin or identify one cause for all
missed reception.

Investigation identified a deterministic precision-receiver bootstrap rejection
at a 30 Hz offset before dwell timing qualifies: the conservative untimed model
can prevent the frequency reference needed for assistance. The later diagnostic
below does not retroactively pass this failed run. Exact command, resolved
settings, progress, deadline and stderr are retained in
`local-evidence/helper-v3/current-simulation/crystal-final-command.log`,
`crystal-final.jsonl` and `crystal-final.stderr`.

### Pre-containment HF recovery and incomplete EME diagnostic

Before the fixed-epoch containment policy, the same HF simulation on overlay
`22125d5b` recovered ordinary raw bits `001`, then their fixed-dictionary text
`e`, in the same GUI row, ID 2. The
three-bit pending row was observed at 70.340 s and its completed text at
70.381 s. Ordinary physical END was observed, but the earlier prefixes `0` and
`00` were not separately observed. The search-limited health state also remained;
the probe explicitly reported `receiver_health_ok=false`,
`all_pending_prefixes_on_same_row=false` and exit 1. This demonstrates ordinary
payload recovery in that case, not complete clean progress/health qualification,
a CPU speedup, general weak-link sensitivity or GPSDO replacement. This earlier
primary-route result cannot establish assistance benefit for the current rolling
route, which retains the ordinary bank. The reported HF and EME commands select
Auto and are ineligible for current primary replacement; any benefit through the
additional lane needs its own evidence. The command
and observations are retained in `current-simulation/crystal-recenter-command.log`
and `crystal-recenter.jsonl` under `local-evidence/helper-v3/`.

The exact EME diagnostic on that same overlay was cancelled after its last
observation at 178.998 s wall time and only 1.074 s media. No ordinary bit or
payload END was observed. The ordinary search requested 9,432,739,510 bytes of
DSP buffering before a sufficiently long helper observation could condition
acquisition, and reported incomplete half-chip clock/frequency/rate coverage.
This was a cancelled/incomplete diagnostic, not a completed receive or deadline
pass. The cancellation record and original command/progress remain in
`current-simulation/eme-recenter-cancelled.json`, `eme-recenter-command.log` and
`eme-recenter.jsonl` under the same evidence directory.

### Adjacent physical transmission and cable return

The actual 40 W helper/4 W payload test used 48 kHz hardware with the nearest
helper center at 2,062.5 Hz and data center at 1,500 Hz. The public Volume 100
run completed TX and recording but returned 122,056 samples at or above the
16-bit clipping threshold, with peak 1.0 FS. It is retained as a clipped return,
not clean-signal qualification.

A separate repetition using the public Volume control at 20 completed TX and
recording with exit 0, zero clipped samples and peak 0.33835 FS. Returned helper
RMS was 0.13502 FS versus payload RMS 0.04351 FS: a 9.8358 dB ratio for the
configured 10 dB. This establishes adjacent-tone output and clean cable return
at that Volume setting. It does not qualify receiver decoding, RF power
calibration, weak-link assistance or GPSDO replacement. Both original and clean
repetition results, waveforms and analyses are retained in
`local-evidence/helper-v3/tx/gpsdo40-adjacent-final-*` and
`gpsdo40-adjacent-volume20-*`.

### Source-bound native receive completion with pending-poll gap

A separate normal-Controller physical duplex run linked the frozen `22125d5b`
libraries, using public Volume 20, 48 kHz hardware and the 2,062.5 Hz helper.
TX completed 17.4002 s media at 19.0556 s elapsed. The receiver exposed the exact
ordinary raw bit `1` as completed at 46.7336 s, row ID 2, revision 1, with no
unexpected bits. Capture continued until about 110 s, so completion occurred
during actual physical capture rather than at EOF. The observed run had no
receiver-health fault, drops, overruns, backlog or errors.

The strict wrapper nevertheless exited 3 after 110.110 s because it never
observed pending `1` in its polls; it reported no pending-to-completed same-row
coverage. This is completed physical receive evidence, not a passing full
wrapper or next-poll progress qualification. It also makes no weak-RF or GPSDO
replacement claim. The wrapper binary SHA-256 was
`88c141a7278c96d7fe590ece75a316c7678f246994dfac7a9337cb5e91d926b8`.
Command, output and failure result are retained in
`local-evidence/helper-v3/tx/gpsdo40-rx-overlay22125-volume20-command.json`,
`gpsdo40-rx-overlay22125-volume20.log` and
`gpsdo40-rx-overlay22125-volume20-result.json`.
`receive-22125-build-binding.json` records the exact source/library binding;
`receive-22125-postrun-hashes.json` confirms all linked/runtime inputs remained
unchanged. This evidence belongs to pre-containment `22125d5b`, not later
bootstrap or containment changes, and does not qualify current rolling-route
assistance.

### V3-source physical duplex — completion observed, prefix coverage incomplete

The current `107aa9d0` source and linked libraries were checked through the
actual shared Controller and native audio path, at 48 kHz, public Volume 20,
40 W helper/4 W data and the planned 2,062.5 Hz helper. All 42 source inputs
and linked binary hashes were unchanged. The hashed GUI ELF was not launched
through a toolkit in this physical check. The transmit result reported
`passed=1`, with 45.8446 s of media; the 110 s WAV recorder exited 0 and cleanup
completed. The overall strict runner nevertheless exited 3, as detailed below;
this was not a transmit failure.

Helper row ID 1/revision 2 was pending at 1.8561 s and received at 7.6607 s.
Ordinary row ID 2/revision 2 first appeared with exact `001` pending at
46.4406 s, then completed as `001`/`e` on that same row at 77.8076 s, with no
missing or unexpected bits. Capture remained open to 110.012 s, so completion
did not depend on EOF. No receiver-health failure was reported. The separate
`0` and `00` pending prefixes were never observed in the polls, so the strict
all-prefix predicate failed and full individual-prefix/next-poll coverage
remains unqualified. The observed `001` pending-to-completed transition is
verified; it is distinct from that remaining coverage gap.

The returned PCM had zero clipped samples and a peak of 0.337677 full scale.
Measured helper/data centroids were 2,062.4894/1,500.9495 Hz in disjoint bands,
with a 9.9925 dB RMS power ratio against the configured 10 dB. This establishes
clean adjacent cable output and the observed ordinary receive completion,
not weak-RF, default-Auto or GPSDO-replacement benefit. Binding, strict result,
spectrum and original output are retained under `local-evidence/helper-v3/tx/`
as `current107-physical-volume20-binding.json`,
`current107-physical-volume20-result.json`,
`current107-physical-volume20-spectrum.json`,
`current107-physical-volume20-tx.log` and the matching WAV.

### Withdrawn bootstrap proposal

The unaccepted recipe bootstrap was withdrawn from production after independent
review identified a native unprocessed-queue (`Q - R`) overrun and unreserved
simulation DSP use. Its patch is retained in ignored evidence for future
experiments; this withdrawn path is not implemented behavior and does not fix
or qualify the exact weak EME case. A separate private joint raw-bin frequency
estimator remains unaccepted, with no production integration planned for this
candidate. Payload-contamination handling and its stochastic phase prior need
a supported argument and validation before any future integration. Completed
private diagnostics do not establish production payload SNR, CPU speedup or
full-case qualification.

The safe existing assistance routes remain optional: full ordinary receiver
grants, retained history and the conditional receiver must coexist inside the
configured resources. If they cannot, helper work yields while ordinary
framing, source quota, progress and fully scored physical END remain unchanged.
The production resource contract remains unchanged: no additional RAM allowance,
source-quota change or disk/I/O contract change. The review rejection and
original diagnostic failures remain distinct from future qualification.

### Normal regression results and remaining qualification

The normal application/GUI/worker build passed with exit 0 using
`./build.sh --web-worker --build-dir build/dev --jobs 2`, bound to the current
42-input manifest by `final/containment-application-build-result.json`.
The 294-registration normal run completed and was reaped with exit 1. Original
outcomes are 288 passes, five failures and one timeout, all accounted for in
`local-evidence/helper-v3/final/normal/reconciliation.json`:

| Scope | Original normal-run outcome | Follow-up status |
| --- | --- | --- |
| Contract | 208 passed; one preparation timeout | Failed/incomplete preparation coverage |
| Shared GUI extras | 33/33 passed | Complete for this selection |
| Regular extras | 36/36 passed | Complete for this selection |
| Web | 11 passed; two socket-permission setup failures | Exact pair passed on loopback-permission retry |
| Native GUI | Three immediate display-startup failures | Exact display-only retry passed 3/3 |

The reconciliation repairs reporting of CTest output with adjacent `***` status
text; original logs and failed outcomes are unchanged. The contract's
`scheduled_parallel_preparation` timed out at 1,500.12 s, at line 3157 of
`local-evidence/helper-v3/final/normal/contract.log`. The unchanged watchdog and
this failure preclude a full contract or matrix pass. The preceding input
closure equivalent to committed `b2d3539` also timed out at 1,500.05 s; neither
result establishes passing preparation throughput. The overall run log is
`local-evidence/helper-v3/final/normal-qualification.log`.

The web retry ran only `build_preview` and `web_preview`: both passed in 7.50 s
with the same 42-input overlay verified before and after. The original
`PermissionError: [Errno 1] Operation not permitted` failures remain recorded.
Retry accounting is in
`local-evidence/helper-v3/final/normal/web-loopback-retry-reconciled-reconciliation.json`.
Original native startup failed because Xvfb could not bind its listeners and
the tests could not open the display; adapter assertions did not run then.
The exact display-only retry passed all three tests in 225.14 s, including
`gui_workflow` in 139.74 s, with unchanged source, binaries, tests and deadlines.
Its result is in
`local-evidence/helper-v3/final/normal/native-display-retry-reconciliation.json`.
Across the original run and these five strict setup retries, 293/294 distinct
cases passed. The preparation timeout remains unqualified; the retries do not
turn the original 288-pass/five-failure/one-timeout result into a passing full
matrix. Normal CPU work is complete. The current-source physical check above
observed clean output and same-row `001` pending-to-completed reception, with
strict individual-prefix coverage incomplete and runner exit 3.
`local-evidence/helper-v3/final/final-verification.json` confirms the 42-input
overlay, all five accepted application/archive hashes, complete registration
accounting and cleanup of the normal, retry and physical processes. Docs-only
changes are excluded from the source guard.

The focused sanitizer coverage does not replace the full sanitizer suite.
The pre-containment HF recovery leaves progress and health coverage unresolved;
EME needs a complete run. Current physical `0`/`00` next-poll coverage and full
duplex qualification remain unqualified despite the observed `001` transition
and physical completion.
A proposed 64-point frequency history failed known-frequency truth coverage at
24 dB-Hz in `local-evidence/helper-v3/eme-history/regression-history64.log`.
That history proposal remains unaccepted; the recipe bootstrap above is
withdrawn, and the private joint estimator is not qualified. None is a passing
weak-EME fix. Current-source qualification is limited to the results recorded
above; prior diagnostic snapshots and binaries do not qualify it. In-progress,
failed or cancelled work is not a pass.
No weak RF-link or 5.8 GHz propagation benefit is established by these fixtures.

Unknown full-band helper discovery, arbitrary UTC/private-epoch ambiguity and
every repeated-message onset are not solved by one tone. Dynamic phase/timing
assistance requires independently qualified measurements and supported geometry.
True scheduled, Symbol_Burst and deferred hopping retain ordinary fallback when
assisted acquisition is unavailable. Physical helper sidebands, colored
interference and DAC/RF dynamic range remain limitations at extreme power
ratios. A tone cannot separate RF error, Doppler and sample-clock error. Windows
runtime, real browser, prepared Wasm/SDK, alternate frontends and release/package
certification remain separate scopes.

### Historical Helper Carrier evidence — not current qualification

The previous candidate was based on
`b47c7a0ebd6a29d21ffa8748d5bc7ffbbeb7e942`, with 44-input overlay
`d9f1212860bd02830b1d9bab2e27141882a29273804f30c34508e1f017d9b0dd`, recorded
in `local-evidence/helper-live-fix/final-source-inputs.json`. That implementation
was incorporated into the current `b2d3539` base; its results do not qualify the
new placement, FIR, conditional bank, precision or metadata changes above.

Its normal contract run passed 206/207 in 3,576.19 s. The unchanged
`scheduled_parallel_preparation` watchdog expired at 1,500.05 s while
`shared.serial` was active. The 44 recorded input hashes for that run match
committed `b2d3539`; this is the exact preceding baseline timeout. A separate
earlier `helper-carrier-v2/contract.log` run timed out at 1,500.11 s in
`shared.parallel`, but its candidate inputs differ from `b2d3539` and are not an
exact baseline comparison. Neither run established a full contract pass or
preparation throughput parity. The earlier source also passed 39 shared GUI
cases, all 13 web registrations after two sandbox socket-setup failures were rerun with
authorized loopback access, 36 regular cases outside contract, nine focused audio
registrations and three native GUI cases on a private 2,400×1,800 display.
Its regular/contract union was 236/237, with the same preparation timeout.
The original desktop adapter-height failure, socket setup failures and passing
reruns remain in the retained `helper-live-fix/final-*` logs.

The earlier physical 48 kHz cable return measured a helper near **6,380 Hz**,
9.87 dB above the payload for a configured 10 dB ratio, with zero clipped
samples, and an encrypted one-bit duplex repetition passed in 50.07 s.
Separate Low/cochannel controls also returned their tones. These measurements
used the superseded distant High placement. They are historical audio evidence
only and are explicitly excluded from current adjacent-placement, current
physical transmission and sensitivity claims.

The old 52-configuration observer benchmark and focused five-suite ASan/UBSan
checks likewise precede this candidate. They do not establish current heap,
CPU margin, weak-frequency precision or sanitizer coverage. The prior sanitizer
scope excluded Live and dependency instrumentation; disabled LSAN was not leak
coverage. Earlier invalid planner, frequency, suspension, noise-tail,
helper-loss and late-private-epoch failures remain retained. The old strong
calendar fixture checked one advisory epoch; the new multi-epoch conditional
route needs its own qualification. Historical helper-only success never
established full mixed-link sensitivity or RF propagation performance.

## Prior source qualification

Tested source: `e9763dc0ab84b31d3933a54f3547690a2a7fbbde`.
Documentation/data commit `0751ac847096de8017d7028d14f22037abce445e` has identical
application, test and workflow inputs. Both full runs used standard runners and
`devfast=false`; required inventories and source identities were independently read.

| Scope | Exact run and result |
| --- | --- |
| Full native | [39899129455](https://github.com/mirage335-colossus/pumpModem/actions/runs/39899129455): 48 required jobs successful; 121m48s. |
| GNU 11 Debug ASan/UBSan core | 268 configured cases: 251 passed, 17 declared return-77 omissions, zero failed or incomplete. |
| Linux / Windows Release core | Linux 254 passed / 17 declared omissions; Windows 233 passed / 33 omissions. |
| Full SDK | [39899129083](https://github.com/mirage335-colossus/pumpModem/actions/runs/39899129083): 15 required jobs successful; 58m25s; exact 200-test contract union and eight copied-archive smokes passed. |
| Physical / GUI coverage | Original assertions passed. Debug EU stop 8.48064s < 10s; source advance 3.18854s <= 3.26806s allowance; protected tail 3.14681s passed. |
| Preparation / calibration | Complete original cases, fixed seeds, serial/parallel comparisons and combined statistical gates passed. |

Live compact coverage completed in all three native configurations in this cohort.
Existing instrumented `fast_session` / `gui_fast_live` omissions and opt-in
sanitizer desktop omission remain explicit; omitted checks are not passes. Existing
ordinary five-second stop, delayed-arrival, completed CPU-work and Rev cadence
warnings remain visible. Physical GPM and device audio were not exercised by these
simulations. SDK results do not replace native GNU 11 sanitizer coverage.

The 25% CI margin applies only to explicit runtime and absolute-footprint
benchmarks. Local thresholds, ordinary functional assertions, numerical results,
configured allocation limits, six seconds of fully scored physical absence and
ten-second graceful stop remain strict. See the
[precise margin policy](building.md#resource-benchmark-margins).

## Symbol_Burst and supplemental corrections

The prior reconciliation is complete, not pending because an older narrative
said a run was still in flight. Runtime `83975bd8054f336a5aaa01205b8f54db6d5b13f9`
was qualified through source `67db103c2b59f72b97e07447c27c6d453aac68de` by
[native 39742621396](https://github.com/mirage335-colossus/pumpModem/actions/runs/39742621396)
and [SDK 39742625503](https://github.com/mirage335-colossus/pumpModem/actions/runs/39742625503).

[Paced backlog qualification 39737595013](https://github.com/mirage335-colossus/pumpModem/actions/runs/39737595013)
passed all nine plaintext/keyed, Burst Off/On and held/unheld cases, the original
no-argument Burst test and three storage tests. Exact bits, complete input,
physical END, final frontier/queues and cleanup passed. Held-On cases cleared 14
plaintext and eight keyed gaps; Off controls made no rescue claim. Maximum callback
was 0.906497ms and start lateness 1.323429ms; original 20ms start/completion guards
passed. Generated/prefetched samples are not transmitted progress. Reported PCM/FNV
identities matched; raw PCM was not retained for independent rehashing.

Later runtime `c51183182c6c68b5a260fba4712bb9819f943996` corrected receiver
acquisition completion and ncurses readable-prefix handling. A matched complete
PCM fixture recovered all 32 Help bits where baseline retained a 17-bit suffix;
this does not prove a unique cause for every historical GUI failure.
[Strict GUI 39859510712](https://github.com/mirage335-colossus/pumpModem/actions/runs/39859510712)
passed the original pair 2/2 serially and 2/2 in parallel, with EU stops
8.5334/8.2779s and unchanged tail/idle assertions. Its inputs match the current
runtime; this evidence remains bound to its actual source. Full candidate
`cefdac53617105aa120e425f1b270a9035777493` passed
[native 39880745813](https://github.com/mirage335-colossus/pumpModem/actions/runs/39880745813)
and [SDK 39880749070](https://github.com/mirage335-colossus/pumpModem/actions/runs/39880749070)
before the current resource-margin qualification.

Original failures remain failures: copied Help, strict compact Live compute,
SDK paste-send at its 20-second deadline, Release Burst at 51.83s, timing advice
beyond three seconds, and cancelled APT setup. Passing later results neither erase
those failures nor identify a sole cause. Earlier compact-Live incomplete coverage
is historical, not a pass and not the result of the current completed cohort.
Detailed original records remain in the pre-cleanup Git history and retained local
reports. Sched_Hop has not demonstrated the separate sustained-performance objective.

## Prior cohort CI elapsed time and remaining uncertainty

The qualified simultaneous native/SDK pair took 121m49s, with 1424.33 unweighted
runner-minutes and 365775353 artifact bytes. The preceding `cefdac5` pair took
113m11s, versus 173m46s for the earlier `67db103` pair. These are observed complete
qualification times across different source epochs and runner cohorts, not matched
causal estimates. Queueing is included. No persistent compiler cache was deployed.

Adding the boundary test changed 245 existing round-robin assignments. Prior
per-test wall sums shift about 882s toward Debug core part 0, which became the
current final path. These sums do not predict job elapsed time or separate build
cost, runner variation and contention. The latest 8m38s pair increase alone does
not establish a persistent regression. Reuse the existing plans/timings before
considering a bounded rebalance; do not repeat full baselines just for comparison.

The confirmed 20-job account limit can queue the full native/SDK fan-out.
Independent package producers and copied checks proceed without waiting for core,
calibration or optional instrumented GUI scopes. Complete-release elapsed time
has not been measured on the latest branch. The bounded
[resource snapshot](ci-resource-results.json) distinguishes CPU, wall and workspace,
with unavailable fields null; Git retains its previous versions.

## Publication and exact-binary certification

A read-only GitHub check on 23 October 2026 still identified
[`v001_00-2026-09-30-0648CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-30-0648CDT)
as ordinary Latest, source `dce9138d27a2e6941b4304af2cf035c4ac2a6c9b`.
Its certification run 36704181844 attempt 1 failed; attempt 2 passed with warnings.
The exact Windows WGL exclusion and other recorded limitations remain; see the
release's hash-bound reports. The newer branch's green CI has not published or
certified replacement binaries.

For an authorized delivery, follow [RELEASE](../RELEASE) and
[the release procedure](releases.md): pin the candidate, complete required native
and SDK qualification, produce and check packages, publish, certify the exact
source and delivered binary hashes, then verify GitHub Latest. This cleanup does
not authorize publication or paid runners. Preserve prior release assets/reports.

## Automatic regression signal policy


Automatic regression checks must not force `tone-N`, `auto-tone`, or
`SpreadingMode::tone` fixtures or require successful tone simulation/reception.
Use changing-sign fixed or seeded pseudorandom patterns to check differential
phase and amplitude measurements, acquisition, streaming memory and timing.
Keep measurable transitions in signal fixtures; changing a mode name while
leaving a constant one-chip code does not add pattern-transition coverage.
Mode-name parsing can still cover the complete supported option catalog.
Independent sine-wave checks of resampler/filter mathematics are not modem
tone-pattern reception tests.

Tone operation depends on narrower physical conditions, such as GNSS timing
synchronization, low frequencies, high symbol rates and suitable hardware.
Successful differential measurements with pseudorandom patterns exercise the
measurements needed for tone shifts under those conditions; automatic tone
loopbacks are not a requirement or evidence of general tone reliability.
Historical tone results below are retained as records, not current regression
requirements or hardware validation.

## Historical evidence

The former run-by-run ledger is recoverable at
[0751ac8:docs/validation.md](https://github.com/mirage335-colossus/pumpModem/blob/0751ac847096de8017d7028d14f22037abce445e/docs/validation.md).
Read the relevant dated section when investigating a regression; it is not a
startup reading requirement. [Release history](release-history/2026-09-30/README.md),
active independent fixtures remain intact. Retired numerical captures remain
available through pinned Git history. Historical
binary hashes identify former executions; removed local binaries must be rebuilt
from their matching source/configuration before a new experiment.

Ignored `local-evidence/` retains the latest qualification reports, unaccepted
patches and selected receiver/CI inputs needed by unfinished work. It is not a
build-history archive. Completed study data and superseded tracked narratives
are available through [pinned Git references](indexes/evidence.md); old local
artifact paths may have been retired. Current development needs current source
and focused evidence, not reconstruction of former build environments.
