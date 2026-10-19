# Private capacity-shape diagnostic

This prepares one diagnostic overlay against immutable source commit
`81c7da84121be60fe0ce1e7252e94cdf952773e8`. It does not alter the checkout.
The script requires the exact canonical `tools/owner-census-diagnostic` producer,
extension, and header pins recorded in its manifest. It writes a fresh output
folder containing `prepared-source/src/live.cpp`,
`prepared-source/src/pattern_receiver.cpp`,
`prepared-source/tests/test_live.cpp`, both support headers, reversible CPPs,
patches, and `manifest.json`.

Prepare with:

```sh
python3 prepare.py --source-root /absolute/source/archive --output-dir /absolute/fresh/output
```

Build using the existing isolated instrumented Debug/ASan+UBSan route, with the
prepared `live.cpp` and `test_live.cpp` overlaid into that archive. Then invoke
exactly one original fixture process:

```sh
DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC=1 DATAPUMP_OWNER_CENSUS_DIAGNOSTIC=1 \
  "$TEST_LIVE_BIN" --simulation-media-clock-trace "$TRACE_PATH"
```

Keep the original exit status and require the existing strict trace footer,
external flush/close seal, zero drops, zero caps, and zero writer errors. The
source transmitted-sample endpoint/generated-bit fields are separate from
receiver sample progress.
The 512 MiB Settings and transfer DSP ceilings change backend, caches, and bit
allocation; this wave answers capacity shape and owner count only. It is not a
matched performance comparison or qualification.

`capacity_shape_point` is a bounded aggregate at joined bank construction
boundaries, emitted on epoch change or owner/byte/admission/retirement growth.
`capacity_shape_summary` gives sampled byte maxima and per-case final state.
Receiver owner peak is exact from the bank's monotonic peak counter and is
reported beside sampled owner counts. Byte peak is sampled at joined refresh
boundaries; it is not claimed as a global allocator peak.
`capacity_shape_transmission` joins TX epoch and source/bank coordinates with
existing sample and generated-bit counters. `capacity_shape_channel_origin`
records the actual channel seed/settings, received/bank origin, startup offset,
and carrier phase at the existing burst boundary. The trace filter is enabled
only by the shape marker and runs before event sequence/attempt counting. It
keeps case, shape, census, selected-backend/admission/retirement/refusal, and
shared-capacity events while omitting per-bit confirmation/history and other
high-volume diagnostics; those omissions are intentional, not dropped rows.
The existing schema-2 `owner_census_*` events keep their first-refusal
semantics. The overlay also applies the accepted PatternReceiver inspector at
its original `synchronized()` hook, so a refusal census can report retained
track and pattern state. A latest predicate journal whose rows do
not cover the resident count is explicitly incomplete; it does not classify
newly admitted owners. Owner-local PatternReceiver phase/timing bit limits are
not captured by aggregate points when no first-refusal census occurs; existing
`receiver_selection` records preserve selected backend and admission sizing.
