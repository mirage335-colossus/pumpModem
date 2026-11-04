# Completed scientific records

This bundle preserves completed records for the broader performance study. It
does not combine their durations into one measured campaign or imply completion
of an active complete-suite run. Controls and native coverage
are archived by their respective owners outside this subtree.

| Record group | Observed result |
| --- | --- |
| Planner routine 001 | Eight complete direct targets, 42.634946 s; functional and prediction checks passed. |
| Planner diagnostic | Separate Markov/Chernoff design-bound diagnostics on the retained sampled workspace; not additional routine qualification. |
| Python coverage 001 | 63.193551 s, 17 measured Python sources and 43 declared dependencies; retained branch/line misses and actual test output. This is declared dependency coverage, not discovered dynamic import closure. |
| GPU legacy qualification 001 | CUDA backend 392.871805 s, outer run 395.202461 s; 14 prediction-valid targets, no failures, five warnings retained. |
| GPU historical staged replay | Seven of seven stored mapping/proof checks true; historical replay on the matched numerical stack, not current hardware qualification. |
| GPU hardware profile | Completed hardware observations retained with their own scope and uncertainty. |
| GPU lifecycle qualification 002 | Passed, 882.041189 s; two cold cases and a mixed campaign. Setup attempt 001's precreated-output failure log is retained separately. |
| CPU lifecycle qualification 001 | Incomplete after 1800.536065 s and a worker deadline timeout. Cold case passed; five complete mixed validation pairs remain inconclusive. The partial sixth pair is excluded from the analysis. |
| Resource qualification 001 / 002 | Geometry-preflight error at 10.602019 s / inconclusive at 15.549705 s; retained as unsuccessful attempts. |
| Resource qualification 003 | 175.835208 s; sequential regime qualified, paired regime remained inconclusive. |
| Resource direct 001 | Passed, 14.467303 s, complete direct measurements and valid replay; no forecast or computational-savings claim. |

The GPU warnings are an unresolved full-runtime comparison for
`native-fhss-selective-burst` and unmet requested timing precision for
`native-null`, `native-frontend`, `vlf-850-day`, and `gaussian-multiblock`.
Prediction validity does not erase these warnings or establish regression
decisions. Timing boundaries, uncertainty, observed work, and policy decisions
remain in the exact original records.

`preservation.json` maps every original record to an exact gzip payload or a
byte-exact reconstruction. In particular, the CUDA report is reconstructed from
the outer report's `backends[0]` with the recorded JSON formatting; its original
byte length and SHA256 are verified. Identical payloads are stored once. Gzip
headers have zero timestamps and no original filename.

`additional-preservation.json` appends the lifecycle/resource records and GPU
native build provenance. Large JSON records use compact JSON gzip payloads or
references to values already retained in the summary. Reconstruction restores
the exact original indentation, newline, byte length, and SHA256; the verifier
checks every original record. The launch receipt for GPU complete routine 002 is
included, but its active result is not archived as a completed run.

`source-index.json` binds versioned members of `matching-source.tar.gz` to the
source/config hashes in the raw records. The snapshot includes the planner build
freeze, the historical GPU upload overlay, and the exact coverage matrix used by
the coverage run. It excludes the moving current matrix and top-level study
README. The listed source closure is not a claim of transitive compiler or
dynamic import closure.

`additional-source-index.json.gz` binds the exact lifecycle `35e543ac` protocol,
resource source freezes, and measured function bodies. Its AST receipt checks
record the empty-field representation needed to match Python 3.12 and 3.13
hashes. These checks require Python 3.13 or later. Resource attempt 001's original
driver (`f388816d`) was not retained: its failed record and source digest remain,
and no reconstructed driver is substituted. This gap belongs to the failed
preflight attempt, not the later accepted direct measurement.

`input-verification.json` records archive-time checks of all 28 retained files
for six physical input workspaces against their run-recorded digests. Physical
arrays and executables are intentionally omitted. Exact manifests, generation
arguments, seeds, native binary identities, planner compile commands, build logs,
and matching CMake caches are retained. Historical staged anchors remain in their
original archive and are linked by digest in `external-references.json`.

GPU native compile commands, CMake caches, compiler identity, and binary digests
are retained and match the GPU reports. Exact lifecycle qualification 002 outer
argv was not retained. Its member commands, profiles, seeds, static bindings,
and original source are preserved; no reconstructed command is labeled as an
original invocation. No benchmark was rerun to create this archive. Use
`python3 verify.py` from this directory for integrity checks.
