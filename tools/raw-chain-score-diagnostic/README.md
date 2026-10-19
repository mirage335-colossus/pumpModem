Private component score diagnostic, pinned to application source 117538bd7d946de88d481fc8ab82e022d803dd8d.

This is an observation overlay for reproduced RAW failures. It changes no receiver admission rule, threshold, wire format, input, EOF/end policy or production checkout. Component exit zero is not application qualification.

Prepare from the pinned isolated source copy:

```
python3 prepare.py --source-root /absolute/isolated/source --output-dir /absolute/fresh/overlay
```

Copy only emitted `pattern_receiver.cpp` and `raw_chain_score_probe.hpp` to the isolated source copy's `src/` before its existing configure/build. Retain `manifest.json` and `pattern_receiver.reversed.cpp` as artifacts. The tool rejects altered source closure or existing output; original source is read-only. The launcher revision may include only diagnostic wiring in addition to the pinned application closure.

Build one binary. Run the existing `--simulation-media-clock-raw-component <absolute-fresh-output-dir>` twice with `DATAPUMP_RAW_CHAIN_SCORE_DIAGNOSTIC=0` and `=1`. Keep ordinary standard GNU11 Debug ASan/UBSan options. Runtime checks are owned by the root workflow, not this preparation tool.

Existing PCM/report/trace outputs and strict trace finalization are unchanged. Both public component reports and PCM must match each other and the accepted component run 39235964462 (PCM SHA256 c75c5b0e17ca671e25b06bb87e3a959729e8ffda5753659b805cca321b30dd80; N=99379/source=41409) before using score observations. Preserve all older default-clock, raw-content and framed-capacity failures. Cross-process timing comparisons with old runs do not establish overhead.

Additional trace events:

- `raw_chain_score`, schema 1, stage `seed` or `step`: receiver epoch/input origin, physical and stream identity including phase, pending n/SUM and actual added score, raw best/alternative scores, cumulative penalty, computed bound/threshold/trials and selected timing/carrier geometry. Step records are emitted before the admission branch resets pending state. Support weights are labeled arbitration data, not admission evidence.
- `raw_chain_score_summary`, schema 1: emitted from each selected receiver's Impl destructor inside the component's outer trace Scope. Off and on each require exactly one summary for each receiver pair. Mode, all eligible/attempted counts, filter cap, invalid clocks, matched emission-scope CPU/wall and clock-read counts are disclosed.

Only (epoch1792403097, input origin0, physical first8496) and (epoch1792403100, input origin10009, physical first16321) are captured. Each receiver has a hard 256 seed/step record cap. A nonzero limit, missing ordinal/summary, invalid mode/clock, dropped/limited trace or missing footer/external seal means incomplete diagnostics. An absent filtered event is not a negative result.

Timing scope: four explicit clock-read attempts per emitted probe (two steady, two same-thread CPU); trace emits one additional steady read per probe and summary. Summed probe CPU/wall covers the matched record construction/enqueue only. It excludes filter checks, after-stamp accounting, summaries, other receiver work and whole-process runtime. Off/on wall and CPU should come from the same external wrapper and binary. These counters alone do not prove whole-process overhead or CPU savings.

No selected-fit score record establishes an iid/central null or adaptive-path union proof. Dot/norm/Gram data and a predetermined unselected panel are not captured. Any math change remains a separate consequential decision after actual component evidence, not an interpretation supplied by this overlay.
