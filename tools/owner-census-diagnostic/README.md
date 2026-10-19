Private first-omission owner census, schema 2, pinned to source `73a94aa69f201cc295d695b45dfa023e53151302` (application closure unchanged from `a9b2d95a5fca8f7f007d84a9269cac3863da65c3`).

Prepare a fresh overlay from an isolated source archive:

```
python3 prepare.py --source-root /absolute/isolated/source --output-dir /absolute/fresh/evidence
```

The preparer reads seven exact source inputs, emits two patched C++ files, their exact reverse reconstructions, patches, a private header, field contract and a manifest. Verify the manifest and copy only `live.cpp`, `pattern_receiver.cpp` and `owner_census_probe.hpp` into the isolated archive's `src/`. Keep the production checkout intact. A later launcher commit may contain diagnostic tools/wiring in addition to this pinned application closure; retain its exact commit and full archive hash separately.

Build the existing GNU11 Debug ASan/UBSan `test_live` with Portable OFF. Run one original `--simulation-media-clock-trace` fixture with `DATAPUMP_OWNER_CENSUS_DIAGNOSTIC=1`, preserving its exit code and assertions. This observes the first `unavailable()` or allocation-refusal admission path; it does not cover every independent limited flag. Require the original trace footer and external flush/close seal, with no dropped or limited trace records. See [reader-contract.md](reader-contract.md) and the authoritative producer header for exact field types, conditional fields, counts and completeness limits.

The default clock and natural origin are unchanged. This is a single diagnostic capture, with no matched overhead or fixed-work comparison. Census CPU/wall covers synchronous emission and owner queries only; journal maintenance, filtering, summary and the rest of the process are excluded. Fallback, missing inspection, caps, query errors and unavailable coordinates must remain explicit. Last-refresh decisions are historical observations; unmatched owners and GUI rows cannot establish retirement eligibility. This tool changes no receiver policy, input, physical completion or stop deadline, and diagnostic results do not qualify Live or GUI behavior.

Schema 2 reconciles the stored application ledger with separately measured per-owner and shared charges. It records actual history/peak capacities, optional cache and scratch terms, and the last observed candidate-construction stage. Preserve a stored/current mismatch as evidence. These are application workspace charges and reservations, not process RSS. The preparer binds its extension helper and field contract as well as the producer; verify all four hashes.
