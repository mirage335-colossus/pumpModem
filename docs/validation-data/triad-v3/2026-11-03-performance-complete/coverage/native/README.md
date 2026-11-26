# Native triad coverage evidence

This compact record retains the original non-atomic capture with invalid negative gcov branch counters and the fresh atomic focused capture. It includes both sets of raw compressed gcov JSON, both reports, the pre-build header guards, selected compile commands, and checksums. The eight source/test translation-unit snapshots named in the original package were removed during source cleanup; their former presence does not mean those files are available in this checkout. The [retirement manifest](../../../archive-content-retirement.json) records the cleanup.

The original raw data has two negative branch counts in `src/triad_modem_v3.cpp` (lines 214 and 216). Their cause is **unproven**. The later `-fprofile-update=atomic` capture has no negative branch counters; this comparison does not prove why the earlier values were negative. The original report is retained unchanged as invalid evidence, not a qualified branch result.

The atomic capture recorded eight selected source/test translation units. Their object mtimes postdate the pre-build header guard; the pre- and post-capture header inventories match. These are temporal and dependency indicators, not source hashes embedded in object files, and the snapshots have since been removed.

Counts describe GCC gcov source lines and compiler CFG edge outcomes. CFG outcomes may include compiler/exception edges and are not semantic branch coverage. This archive makes no 100% coverage claim. Uncovered source lines and declared exclusions remain in each report.

See `provenance.json` for exact commands, compiler flags, source/test hashes, test outcomes, and limitations. `selected-compile-commands.json` retains the eight actual C++ compile commands. `SHA256SUMS` covers every archived file except itself.

The retained `SHA256SUMS` includes the original README digest. This current
source-availability clarification changes that document digest; it does not
change the archived coverage data or its recorded hashes.
