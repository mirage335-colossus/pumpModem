# Native atomic coverage follow-up 001

This separate capture extends the earlier accepted core-only atomic report. It instruments the frozen native source scope, including `tools/triad-long.cpp`, and leaves earlier reports unchanged. Frozen coverage-matrix SHA-256: `1860b045b4078da56db5b8928cbb3b71660844a94d0c5da3ddb1eab5382e5a33`.

The focused native CTests passed **4/4**; two regular `build/dev-cli` focused tests passed **2/2**. Five tiny CLI modes (`plan`, `export`, `export-sampled`, `export-stats`, `export-group-stats`) exited successfully, each under 0.08 seconds. The pre-existing `build/dev-cli/triad_long` SHA-256 remained `862b0431f04a3973ce15e7cff9b9959ed45ca36d626e401352db5cf8630c9c6b`.

| Source | Executed / instrumented lines | Taken / instrumented gcov CFG outcomes |
|---|---:|---:|
| `src/triad_modem_v3.cpp` | 181 / 183 | 215 / 302 |
| `src/triad_receiver_v3.cpp` | 185 / 188 | 326 / 524 |
| `src/triad_simulation_stream.cpp` | 558 / 560 | 580 / 814 |
| `src/triad_statistics.cpp` | 242 / 244 | 241 / 376 |
| `tools/triad-long.cpp` | 1171 / 1805 | 1723 / 5412 |

The complete rendered all-invalid receiver fixture covers the rank-zero/infinite-threshold guard. The valid high-rank statistics fixture exercises Gamma iteration exhaustion and conservative fallback. No negative line/branch counters were recorded. The CLI is only partially covered; tiny mode witnesses do not cover every path.

The guard hashes 413 declared dependency inputs (row sources, applicable test paths, project headers, `CMakeLists.txt`, and `cmake/**/*.cmake`), with the matrix file hash separate. A final post-gcov comparison checks those inventories against the pre-build guard. All nine counter TUs were forcibly rebuilt after the guard and have gcda files. This is declared applicability evidence, not proof of complete compiler dependency closure or embedded header bytes.

GCOV reports compiler CFG outcomes, not semantic decisions or behavioral coverage. No semantic-completeness/100% claim is made. The checkout was dirty. Original collector and regular ccache setup issues are retained here.
