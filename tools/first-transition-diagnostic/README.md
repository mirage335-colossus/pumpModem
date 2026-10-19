# First-transition capture runner handoff

These tools prepare an isolated source archive plus reviewed overlay, then run
one original `test_live` process with `DATAPUMP_FIRST_TRANSITION_CENSUS=1` and
`--simulation-media-clock-trace`. The application boundary is the unchanged
Live simulation fixture exercising production Symbol_Burst; the trace is used
to observe first-transition coverage-limiter/fallback state. Original fixture
assertions and its internal functional deadline remain unchanged. This is
single-process diagnostic evidence, not a timing comparison or qualification.

`sourceprep.py` consumes `<overlay-dir>/manifest.json` with this shape:

```json
{
  "baseline_source_commit": "<full lowercase Git SHA>",
  "files": {
    "src/live.cpp": {
      "before_sha256": "<SHA-256 or null for a new file>",
      "after_sha256": "<payload SHA-256>",
      "payloadpath": "payload/live.cpp"
    }
  }
}
```

Only `src/live.cpp`, `src/pattern_receiver.cpp`, `src/audio_trace.hpp`, and
`src/first_transition_probe.hpp` are accepted. The immutable baseline commit is archived explicitly; the launcher HEAD is
recorded separately. Each existing preimage must match both the baseline archive and
checkout bytes. A new file must be absent from both. Every payload is checked
before output is created. The output path must be new. Output contains
`source/`, the original input manifest, normalized `prepared-source.json`,
`source-inventory.json`, copied payloads, and a unified `overlay.patch`.
Inventory covers tracked `src/`, `include/`, `tests/`, `cmake/`, and root
`CMakeLists.txt`, including changed and newly added source files.

```sh
python3 -B sourceprep.py --root CHECKOUT --overlay-dir REVIEWED_OVERLAY --output PRIVATE_PREPARATION
```

Compile `PRIVATE_PREPARATION/source` with the repository's normal focused
`test_live` target. Keep its generated `CMakeCache.txt` beside the binary.
Capture into a new directory (an existing evidence parent may be used):

```sh
python3 -B capture.py --root CHECKOUT --compiled-source PRIVATE_PREPARATION \
  --binary BUILD/test_live --report NEW_REPORT_DIRECTORY
```

The runner starts a fresh trace at `NEW_REPORT_DIRECTORY/first-transition.jsonl`,
retains stdout/stderr, source/binary/cache/compiler identities, safe explicit
environment values and a hash of the full child environment, wall time, child
CPU time, actual exit code, and strict `tools/gui-stop-differential.py::trace_seal`
validation. A sealed trace with test exit 1 remains a functional failure and
returns 1; setup errors return 2; timeout, output overflow, missing/incomplete
trace or changed pinned inputs remain incomplete/fatal. No trace bytes are
rewritten. Root retains interpretation of the event schema and workflow
integration.

No application build, CI workflow edit, Git mutation, or shared source write is
part of this assignment. Inputs that remain unsettled are the accepted Sol
probe payload and root's reviewed overlay manifest; source preparation and
capture must be run only after those land in their agreed private paths.

The retained application baseline is e25748f07be9ec96e8e7f859228f72289ea675ec. The first transition producer is Sol-owned; its schema is in schema.md. Its runtime results remain unverified until a captured run is independently read back. No throughput warning classification applies to the existing functional coverage-limit failure.
