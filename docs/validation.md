# Validation status

Updated 7 November 2026. This records the Robust cleanup candidate, then
uncommitted and based on `357a50970a0c6214eeaade7c5704bbc39abd4b79`, now
committed as `419e6bae8c4fe3b0f59be23a6dfb3b69fbaed80e`. It is not release
certification. The [source and binary inventory](../local-evidence/robust-cleanup/source-binaries.json)
records 2,904 surviving implementation, test and build files and selected binary
hashes. This compact manifest substantiates this cleanup checkpoint; retire it
when the checkpoint is superseded. Its source-inventory SHA-256 is
`2405f259c050b68d7bbabad9628eb97ff750e53c6d9f51012f47530719f7c74b`.
This is a post-validation inventory, excluding documentation/evidence, not a
before/after timing comparison. Raw run logs are temporary; remove them after
this checkpoint is accepted unless a specific active validation or release
requirement still needs them. The concise outcomes and limitations below are
the maintained record.

## Test timing investigation

On 8 November 2026, the dirty working tree based on `64f7feb6` was timed with
GCC 14.2, the native Release FLTK configuration and two test jobs. `test all`
stopped before testing: `test_terminal_ui` and `test_framebuffer` failed to link
native audio symbols. Separate web and build-tool runs also had preview socket
permission failures and temporary GPG-key setup failures. Retain the three logs
in `local-evidence/test-timing-2026-11-08/` to diagnose these failed checks;
remove them when the link and environment failures are resolved or superseded
by equivalent current evidence.

## Transmit key policy checks

The uncommitted policy change based on `64f7feb6f298ba844f45ecbf1c57f02219158d16`
passed `./build.sh test contract` (26/26), `./build.sh test triad` (19/19), and
`./build.sh test gui` (27/27) on 7 November 2026. These runs used the existing
Release `build/dev` profile, two test jobs and four build jobs, with
`CCACHE_DISABLE=1` because the sandbox could not write the compiler cache.
The initial compile also exposed a missing test include, corrected before
these passing runs. Documentation link checks and `git diff --check` passed.

Coverage includes ordinary private/Amateur key parity, temporary noise DATA
keys and forced encryption, public-Amateur Helper decoding, changing callsign
ciphertext across absolute dwells, unchanged Helper vectors, independent
background noise, and the unavailable Robust boundary. Noise-mode coverage is
at the native policy/generator API; no GUI reconnection, operational noise
endpoint, physical transmission, or release certification is claimed.

## Cleanup validation

The CLI, FLTK GUI and pipe-worker applications rebuilt successfully. The FLTK
adapter and document test executables also compile and link; their native-window
runtime tests were not rerun. GUI controls remain, with Robust operation
explicitly unavailable pending connection to the canonical triad backend.

| Final affected check | Result |
| --- | --- |
| `./build.sh test contract` | 25/25 passed, 25.13 seconds |
| `./build.sh test triad` | 18/18 passed, 13.09 seconds |
| `./build.sh sanitize contract` | 25/25 passed, 37.49 seconds; ASan, UBSan and leak detection enabled |
| Final crypto/keyring fixture refresh under sanitizers | 2/2 passed, 8.69 seconds |
| Final focused crypto, keyring, Fast codec/CLI, GUI Fast, web worker/bridge | 7/7 passed, 40.40 seconds |

The triad Python aggregate ran 201 checks, with one native-dependent case
skipped there and covered by its separately registered native test. These are
small correctness checks, not full-observation or performance campaigns.
Independent source comparisons found the retained GF(256)/Reed–Solomon bodies,
Payload keystream path and prepared CBC implementation unchanged by the final
removal of unused recovery, framing and cryptographic APIs. Independent frozen
crypto vectors now exercise the retained prepared CBC API.

Earlier in the cleanup, the shared `regular` group passed 29/29 in 44.08 seconds
after removal of the two obsolete recovery/framing suites. The later crypto
change received the final affected checks above. Legacy passed 8/8 in 1.79
seconds. Web passed 13/13 in 146.47 seconds, including the retained Fast audio
case; the worker and bridge were rechecked after the final crypto change.
The initial Fast and GUI runs passed 35/37 and 26/27 respectively. Corrected
focused reruns completed every previously failing case; all 37 Fast and 27 GUI
cases have passed across those runs, but neither full group was rerun after
all cleanup changes.

The build-tool suite passed 27/32 in the sandbox; the five local socket/GPG
fixtures then passed outside it. Workflow tests passed 3/3, abbreviated-inference
tests 8/8, and certification-tool unit tests 86/86. The maintained-document
path/anchor audit and `git diff --check` pass.

Initial failures remain failed invocations: an invalid tiny channel fixture,
a CLI error exit code, obsolete GUI simulation/readiness expectations, and
missing native audio linkage for adapter test targets were corrected. The
initial sanitizer invocation could not run LeakSanitizer under the sandbox's
ptrace restrictions; the passing reruns retained leak detection outside that
restriction. No omitted check is counted as passed.

## Limits

No RunPod resources, GPU workload, physical audio device, real-browser session,
or release certification was used. The Python acquisition and tracking stages
remain separate stages, not an integrated end-to-end receiver. The bounded
[abbreviated timing harness](triad-performance.md) estimates only declared
fixed-geometry kernel work; it does not qualify physical reception or complete
receiver latency. The smoke measurement withheld an unresolved Gaussian
projection and made no comparative performance-pass claim.

Release assurance follows [the release procedure](releases.md) and binds to
exact published source and binaries. Earlier checks are not automatically
qualification for changed code, configurations or physical devices.

## Historical provenance

Useful [pattern](validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](validation-data/triad-v3/2026-11-05-stochastic/README.md)
measurements remain curated, source-bound research because their conclusions
inform the current triad. Preserve concise results, assumptions, limitations and
needed provenance; do not accumulate duplicate raw captures or parallel
implementations. The
[archive-retirement manifest](validation-data/triad-v3/archive-content-retirement.json)
records removed implementation copies and changed archive hashes; retained
triad data leaves were preserved byte-for-byte. Original source/internal hashes
remain historical provenance, not qualification of repacked archives or current
code. Consult the [evidence index](indexes/evidence.md) and Git history for
superseded results and their limits.
