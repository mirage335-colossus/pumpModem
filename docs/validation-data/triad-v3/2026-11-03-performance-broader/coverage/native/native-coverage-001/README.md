# Native coverage after CMake target addition

This fresh atomic capture supersedes follow-up 001 for current declared-dependency applicability. The CMake change added `triad_planner_perf`; it did not change the five native production source files. The earlier capture still accurately preserves its original counters, but its CMakeLists hash is stale for the current dependency contract. This capture rebuilds the selected nine counter translation units after a new guard. It does not build or time `triad_planner_perf`.

Frozen-at-capture coverage matrix SHA-256: `59c2ed40efea3bcd141ecc4f08579770762d188018ceefb469bdb79ca9900f6a`. The guard records **413 declared dependencies**, including current CMakeLists, headers, row sources and applicable test sources. Before/after dependency and header inventories match; all nine objects postdate the guard, and all nine gcda files exist.

Four focused CTests passed (**4/4**). Seven collector tests passed. Five tiny `triad_long` modes (`plan`, `export`, `export-sampled`, `export-stats`, `export-group-stats`) exited successfully; no mode took more than 0.10 seconds.

| Source | Executed / instrumented lines | Taken / instrumented gcov CFG outcomes |
|---|---:|---:|
| `src/triad_modem_v3.cpp` | 181 / 183 | 215 / 302 |
| `src/triad_receiver_v3.cpp` | 185 / 188 | 326 / 524 |
| `src/triad_simulation_stream.cpp` | 558 / 560 | 580 / 814 |
| `src/triad_statistics.cpp` | 242 / 244 | 241 / 376 |
| `tools/triad-long.cpp` | 1171 / 1805 | 1723 / 5412 |

The receiver test exercises the complete rendered all-invalid rank-zero case. The statistics test covers valid high-rank Gamma iteration exhaustion and conservative fallback. All current line and branch counters are nonnegative. CLI evidence is partial and its many uncovered lines/CFG edges remain listed in the report.

These are GCC compiler CFG outcomes, not semantic branch/condition coverage. Dependency hashes express the declared applicability contract, not full compiler dependency closure or embedded-header proof. Worktree is dirty; no clean-release or semantic-completeness claim is made.
