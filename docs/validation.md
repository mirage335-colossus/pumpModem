# Validation status

Updated 25 October 2026. This page records current qualification, release status,
coverage limits and useful historical anchors. It is not a session log. Replace
superseded status here; keep one latest [resource snapshot](ci-resource-results.json)
and use Git history for comparisons. Detailed studies remain in the
[evidence index](indexes/evidence.md).

## Current Helper Carrier candidate

The current v4 application candidate is v8, overlay SHA-256
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
