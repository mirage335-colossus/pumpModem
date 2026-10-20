# Symbol_Burst: all 944 executions for parallel work on another computer

Prepared **2026-10-20T20:20:28.271887+00:00**. This handoff now enumerates **every one of the 944
executions**, not just eleven issue categories. **All 944 are requested again on
the destination computer**, regardless of whether they passed or are currently
running on the original computer. Nothing is filtered out for that reason.

- **Machine-readable complete list:** [PENDING-TESTS.json](PENDING-TESTS.json).
- **Human-readable complete list:** the 944-row checklist below.
- **Pinned destination candidate:** `e231e4daa727e3943dbc3e184ac86c329a2560e2` (contains the temporal-support
  correction, the batch-test capture repair, and the newer held-rescue fixture).
  Verify it is available on the destination. Never silently follow a moving tip;
  if choosing a successor, pin it once and record it for every result.
- Test names/counts come from exact successful inventories/JUnit on earlier
  `1e9b83238a50bd7983c5b2301f22ac451b42a7b0`. Their old success is provenance, **not exemption from this
  requested destination run**. Hashes, original registration arguments, timeout
  and resource properties are embedded so the ignored evidence directories do
  not have to be copied just to recover the list.

| Queue | Entries | Intended execution environment |
|---|---:|---|
| F-001…F-015 | 15 | GNU11 Debug/O1 ASan+UBSan focused route |
| LR-001…LR-252 | 252 | Linux GNU11 Release core |
| LD-001…LD-249 | 249 | Linux GNU11 Debug/O1 ASan+UBSan core |
| WR-001…WR-231 | 231 | Windows x64 MSVC Release core |
| SG-001…SG-194 | 194 | Prepared Linux SDK/FLTK Release general contract |
| SL-001…SL-002 | 2 | Same SDK, Live contract |
| SC-001 | 1 | Same SDK, full calibration contract |
| **Total** | **944** | Platform/mode repeats count separately |

## Run the whole list, with useful low-cost parallelism

The destination main chat should read the installed delegation policy/current
roles and the authoritative contracts, then use **`luna_worker` (`gpt-6-luna`,
`high`) for suitable independent test-execution shards and settled supporting
implementation**, with `luna_scout` for read-only evidence. Preserve the selected
primary model. Sol retains consequential diagnosis, existing takeovers and
independent acceptance. No global delegation configuration change is requested.

Queue **all 944 entries**. Use as much native-subagent concurrency as genuinely
independent ready work, the destination hardware and current limits support.
Do not restrict the work to one or two issue buckets, and do not cancel an
unrelated shard because another fails. Have workers own coherent test shards
rather than rebuilding the same project once per individual test. Use recorded
historical durations to distribute long cases across suitable independent slots.
Each concurrently writing shard needs a separate checkout/build/output space;
serialize shared Git/integration operations. Build only when actual resource
checks and existing guards permit it. Agents supervising tests are not extra CPU
cores; preserve CTest PROCESSORS/RUN_SERIAL/locks and timing margins.

The manifest contains **941 execution groups for 944 case
results**. Most groups have one case. Focused FFT, GUI serial and GUI parallel
are indivisible two-case groups: run each group command once and record both
case results. The parallel GUI pair must actually execute together with
`--parallel 2`; two isolated passes do not replace that assertion.

Use `execution_groups[task.execution_group].argv_template` as an argv array,
substituting `ctest`, `build_dir` and `result_dir` with verified absolute paths.
Do not shell-eval manifest strings. Prepare builds through `./build.sh` and the
profile's existing workflow recipe, using the correct compiler, backend, SDK
and sanitizer settings. Discover the fresh CTest registrations first. A missing
registration, compiler, SDK or Windows environment is **setup-blocked, not a
pass**. Linux execution cannot satisfy the 231 Windows entries; use a suitable
Windows host/VM or already-authorized Windows runners for those.

A compact worker packet can be:

> Execute every assigned task ID from PENDING-TESTS.json on the pinned revision,
> in its required profile, using the existing acceptance route. You are not
> alone in the codebase; preserve others' changes and write only your assigned
> checkout/output. Do not omit tasks because of original-host progress. Preserve
> commands, deadlines, raw results and failure evidence. Report each ID as passed,
> failed or setup-blocked, never silently omitted. Do not repair an existing Sol
> takeover or reset an original assignment's repair history. Hand consequential
> failures to the original strong owner; keep unrelated authorized tasks moving.

For completion, join **all 944 result IDs** against the manifest: no missing,
duplicate or silently skipped item. Require the original assertions, JUnit and
structured results, exact source/configuration identities and process closure.
The approved narrow warning rules stay explicit; they never turn wrong bits,
missing Help, missing pending data, sanitizer errors or deadline failures into
passes. On failure, preserve that candidate's result even if a later revision
passes; a patched shard is a different source identity.

The existing five-runner ceiling concerns independent **hosted diagnostic
runners**, not a limit of five local read-only agents. Retain actual resource,
authorization and meaningful performance protections. This file neither starts
workers nor changes test/deadline/release policy.

## Scope beyond the 944 subtotal

The 944 are the full previously reported subtotal, **not the whole release
matrix**. In particular, the four newer receiver-held rescue cases introduced
in `e231e4d` and other frontend/web/package/copied-binary jobs remain additional
required coverage where applicable. Do not truncate the current full workflow
to this manifest or replace calibrated aggregate gates with partial samples.
Executing the list on another computer does not itself set a GitHub Actions
checkmark: the required exact-source CI workflows and any requested binary
publication/certification still need their normal acceptance path.

## All 944 named executions

Every row below is pending for the requested destination run. Profile, exact
registration argv, command group, deadlines, resource properties and provenance
are in the JSON manifest. These are test executions, not 944 known defects.

| ID | Profile | Individual test/case |
|---|---|---|
| F-001 | focus_gnu_debug | `symbol_burst_live / original no-argument` |
| F-002 | focus_gnu_debug | `test_symbol_burst_live --paced-plain-off` |
| F-003 | focus_gnu_debug | `test_symbol_burst_live --paced-plain-on` |
| F-004 | focus_gnu_debug | `test_symbol_burst_live --paced-keyed-off` |
| F-005 | focus_gnu_debug | `test_symbol_burst_live --paced-keyed-on` |
| F-006 | focus_gnu_debug | `test_symbol_burst_live --paced-keyed-on-follow` |
| F-007 | focus_gnu_debug | `test_pattern_receiver --burst-temporal-support` |
| F-008 | focus_gnu_debug | `test_pattern_correlator symbol_burst_temporal_support` |
| F-009 | focus_gnu_debug | `test_pattern_correlator_batch / no arguments` |
| F-010 | focus_gnu_debug | `pattern_fft_batch / focused` |
| F-011 | focus_gnu_debug | `search_fft / focused` |
| F-012 | focus_gnu_debug | `gui_timing_advice / serial` |
| F-013 | focus_gnu_debug | `gui_controller / serial` |
| F-014 | focus_gnu_debug | `gui_timing_advice / parallel-two` |
| F-015 | focus_gnu_debug | `gui_controller / parallel-two` |
| LR-001 | linux_release_core | `speculation` |
| LR-002 | linux_release_core | `legacy_psk` |
| LR-003 | linux_release_core | `legacy_olivia` |
| LR-004 | linux_release_core | `legacy_session` |
| LR-005 | linux_release_core | `legacy_simulation` |
| LR-006 | linux_release_core | `legacy_boundary` |
| LR-007 | linux_release_core | `gui_legacy` |
| LR-008 | linux_release_core | `gui_legacy_live` |
| LR-009 | linux_release_core | `gui_legacy_poll` |
| LR-010 | linux_release_core | `vendored_lzma_source` |
| LR-011 | linux_release_core | `scheduled_original_pulse_row` |
| LR-012 | linux_release_core | `scheduled_exact_source_points` |
| LR-013 | linux_release_core | `scheduled_cold_colored_source_rows` |
| LR-014 | linux_release_core | `scheduled_workspace_accounting` |
| LR-015 | linux_release_core | `scheduled_float_spectral_accumulator` |
| LR-016 | linux_release_core | `scheduled_float_acquisition_tile` |
| LR-017 | linux_release_core | `scheduled_cold_colored_runtime` |
| LR-018 | linux_release_core | `scheduled_cold_colored_proxy_runtime` |
| LR-019 | linux_release_core | `live_workspace_capacity` |
| LR-020 | linux_release_core | `scheduled_acquisition_memory` |
| LR-021 | linux_release_core | `scheduled_capture_layout` |
| LR-022 | linux_release_core | `scheduled_carrier_restart` |
| LR-023 | linux_release_core | `scheduled_cold_colored_certificate` |
| LR-024 | linux_release_core | `scheduled_exact_source_samples` |
| LR-025 | linux_release_core | `scheduled_frequency_postinverse` |
| LR-026 | linux_release_core | `scheduled_lazy_acquisition` |
| LR-027 | linux_release_core | `scheduled_memory_projection` |
| LR-028 | linux_release_core | `scheduled_original_projection_cache` |
| LR-029 | linux_release_core | `scheduled_processing_journal` |
| LR-030 | linux_release_core | `scheduled_acquisition_tile` |
| LR-031 | linux_release_core | `scheduled_physical_multidwell` |
| LR-032 | linux_release_core | `scheduled_polynomial_chip_moments` |
| LR-033 | linux_release_core | `scheduled_polynomial_chip_runtime` |
| LR-034 | linux_release_core | `scheduled_polynomial_chip_fft` |
| LR-035 | linux_release_core | `scheduled_integral_colored` |
| LR-036 | linux_release_core | `scheduled_origin_gram` |
| LR-037 | linux_release_core | `scheduled_origin_gram_family` |
| LR-038 | linux_release_core | `scheduled_origin_gram_bulk` |
| LR-039 | linux_release_core | `scheduled_zero_guard` |
| LR-040 | linux_release_core | `scheduled_linear_native_bound` |
| LR-041 | linux_release_core | `scheduled_linear_gain_cells` |
| LR-042 | linux_release_core | `scheduled_linear_direct_runs` |
| LR-043 | linux_release_core | `scheduled_linear_fragments` |
| LR-044 | linux_release_core | `scheduled_default_linear_cellstats` |
| LR-045 | linux_release_core | `scheduled_default_linear_centers` |
| LR-046 | linux_release_core | `scheduled_default_linear_events` |
| LR-047 | linux_release_core | `scheduled_default_linear_runtime` |
| LR-048 | linux_release_core | `scheduled_default_linear_fhss` |
| LR-049 | linux_release_core | `scheduled_default_point_fft` |
| LR-050 | linux_release_core | `scheduled_default_point_observation` |
| LR-051 | linux_release_core | `scheduled_default_point_fhss` |
| LR-052 | linux_release_core | `scheduled_default_point_guard_archive` |
| LR-053 | linux_release_core | `scheduled_default_point_guard_edges` |
| LR-054 | linux_release_core | `scheduled_default_prepared_endstrips` |
| LR-055 | linux_release_core | `scheduled_default_point_guard_archive_runtime` |
| LR-056 | linux_release_core | `scheduled_default_prepared_events` |
| LR-057 | linux_release_core | `scheduled_default_point_fft_runtime` |
| LR-058 | linux_release_core | `scheduled_default_point_family` |
| LR-059 | linux_release_core | `scheduled_default_point_template_cache` |
| LR-060 | linux_release_core | `scheduled_default_point_template_family` |
| LR-061 | linux_release_core | `scheduled_default_point_bulk` |
| LR-062 | linux_release_core | `scheduled_default_point_guard_summary` |
| LR-063 | linux_release_core | `scheduled_default_compact_chords` |
| LR-064 | linux_release_core | `scheduled_default_point_compact` |
| LR-065 | linux_release_core | `scheduled_default_point_reference_recipe` |
| LR-066 | linux_release_core | `scheduled_point_guard_prepass` |
| LR-067 | linux_release_core | `scheduled_default_guard_cohort` |
| LR-068 | linux_release_core | `scheduled_point_source_nodes` |
| LR-069 | linux_release_core | `scheduled_linear_family` |
| LR-070 | linux_release_core | `scheduled_template_cpu_attribution` |
| LR-071 | linux_release_core | `scheduled_fractional_basis` |
| LR-072 | linux_release_core | `scheduled_pulse_chord_certificate` |
| LR-073 | linux_release_core | `scheduled_gain_chord` |
| LR-074 | linux_release_core | `scheduled_pulse_derivative_chord_certificate` |
| LR-075 | linux_release_core | `scheduled_gain_derivative_hook` |
| LR-076 | linux_release_core | `scheduled_fractional_basis_runtime` |
| LR-077 | linux_release_core | `scheduled_cold_owned_proxy` |
| LR-078 | linux_release_core | `scheduled_colored_lattice` |
| LR-079 | linux_release_core | `scheduled_partial_colored` |
| LR-080 | linux_release_core | `scheduled_partial_colored_runtime` |
| LR-081 | linux_release_core | `scheduled_residual_prefix` |
| LR-082 | linux_release_core | `scheduled_acquisition_catalog` |
| LR-083 | linux_release_core | `scheduled_acquisition_quarters` |
| LR-084 | linux_release_core | `shared_scheduled_capture` |
| LR-085 | linux_release_core | `scheduled_certificate_diagnostics` |
| LR-086 | linux_release_core | `scheduled_cancellation_resume` |
| LR-087 | linux_release_core | `scheduled_template_proxy_certificate` |
| LR-088 | linux_release_core | `scheduled_diagnostic_journal` |
| LR-089 | linux_release_core | `scheduled_parallel_preparation` |
| LR-090 | linux_release_core | `scheduled_frequency_piece` |
| LR-091 | linux_release_core | `scheduled_private_batch` |
| LR-092 | linux_release_core | `scheduled_detached_replay` |
| LR-093 | linux_release_core | `scheduled_detached_resume` |
| LR-094 | linux_release_core | `scheduled_fine_curves` |
| LR-095 | linux_release_core | `component_avx2` |
| LR-096 | linux_release_core | `outer_public_replay` |
| LR-097 | linux_release_core | `temporal_entry_payload` |
| LR-098 | linux_release_core | `fp32_packed_l1` |
| LR-099 | linux_release_core | `scheduled_r1_export` |
| LR-100 | linux_release_core | `scheduled_flat_export` |
| LR-101 | linux_release_core | `temporal_outer_plan` |
| LR-102 | linux_release_core | `temporal_fp32_adapter` |
| LR-103 | linux_release_core | `cached_pulse_count` |
| LR-104 | linux_release_core | `pattern_coarse_cache` |
| LR-105 | linux_release_core | `shared_temporal_plans` |
| LR-106 | linux_release_core | `certified_ordered_plan` |
| LR-107 | linux_release_core | `temporal_plan_cache` |
| LR-108 | linux_release_core | `borrowed_plan_prepare` |
| LR-109 | linux_release_core | `temporal_pool_consumer` |
| LR-110 | linux_release_core | `temporal_normalization` |
| LR-111 | linux_release_core | `borrowed_ordered_plan` |
| LR-112 | linux_release_core | `temporal_templates` |
| LR-113 | linux_release_core | `fine_cache_preservation` |
| LR-114 | linux_release_core | `rx_cpu_profile` |
| LR-115 | linux_release_core | `timing_continuation` |
| LR-116 | linux_release_core | `pattern_startup_guide` |
| LR-117 | linux_release_core | `physical_moment_cache` |
| LR-118 | linux_release_core | `pattern_shared_source_curve` |
| LR-119 | linux_release_core | `pattern_permutation_integration` |
| LR-120 | linux_release_core | `scheduled_pulse_moments` |
| LR-121 | linux_release_core | `scheduled_float_signal_moments` |
| LR-122 | linux_release_core | `scheduled_float_gram` |
| LR-123 | linux_release_core | `gram_override` |
| LR-124 | linux_release_core | `scheduled_mapped_piece_groups` |
| LR-125 | linux_release_core | `sched_hop` |
| LR-126 | linux_release_core | `scheduled_symbol_calendar` |
| LR-127 | linux_release_core | `sched_hop_transmit` |
| LR-128 | linux_release_core | `sched_hop_receiver` |
| LR-129 | linux_release_core | `sched_hop_complex` |
| LR-130 | linux_release_core | `sched_hop_correlator` |
| LR-131 | linux_release_core | `scheduled_complex_search` |
| LR-132 | linux_release_core | `scheduled_frequency_fusion` |
| LR-133 | linux_release_core | `sched_capture` |
| LR-134 | linux_release_core | `crypto` |
| LR-135 | linux_release_core | `keyring` |
| LR-136 | linux_release_core | `stream_codec` |
| LR-137 | linux_release_core | `recovery` |
| LR-138 | linux_release_core | `attachment` |
| LR-139 | linux_release_core | `stream_receive` |
| LR-140 | linux_release_core | `boundary_sync` |
| LR-141 | linux_release_core | `compression_short` |
| LR-142 | linux_release_core | `compression_lzma` |
| LR-143 | linux_release_core | `modem` |
| LR-144 | linux_release_core | `streaming_modem` |
| LR-145 | linux_release_core | `transmit_cover` |
| LR-146 | linux_release_core | `noise_receive` |
| LR-147 | linux_release_core | `pattern_code` |
| LR-148 | linux_release_core | `pattern_receiver` |
| LR-149 | linux_release_core | `pattern_correlator` |
| LR-150 | linux_release_core | `pattern_drift` |
| LR-151 | linux_release_core | `pattern_differential` |
| LR-152 | linux_release_core | `receiver_differential` |
| LR-153 | linux_release_core | `pattern_fft_batch` |
| LR-154 | linux_release_core | `pattern_correlator_batch` |
| LR-155 | linux_release_core | `pattern_projection_cache` |
| LR-156 | linux_release_core | `pattern_envelope` |
| LR-157 | linux_release_core | `receiver_strategy` |
| LR-158 | linux_release_core | `pattern_search` |
| LR-159 | linux_release_core | `pattern_start_geometry` |
| LR-160 | linux_release_core | `search_parallel` |
| LR-161 | linux_release_core | `search_arithmetic` |
| LR-162 | linux_release_core | `search_fft` |
| LR-163 | linux_release_core | `pulse_shaping` |
| LR-164 | linux_release_core | `pattern_transfer` |
| LR-165 | linux_release_core | `regressions` |
| LR-166 | linux_release_core | `runtime` |
| LR-167 | linux_release_core | `qr` |
| LR-168 | linux_release_core | `transfer` |
| LR-169 | linux_release_core | `tuning` |
| LR-170 | linux_release_core | `simulation_estimate` |
| LR-171 | linux_release_core | `receiver_probability` |
| LR-172 | linux_release_core | `frequency_axis_probability` |
| LR-173 | linux_release_core | `amplitude_probability` |
| LR-174 | linux_release_core | `differential_probability` |
| LR-175 | linux_release_core | `lpi_estimate` |
| LR-176 | linux_release_core | `correlation_experiment` |
| LR-177 | linux_release_core | `weak_signal` |
| LR-178 | linux_release_core | `live_resources` |
| LR-179 | linux_release_core | `live_receptions` |
| LR-180 | linux_release_core | `live_transmit_lock` |
| LR-181 | linux_release_core | `resampler` |
| LR-182 | linux_release_core | `clock_sync` |
| LR-183 | linux_release_core | `audio_provider_timing` |
| LR-184 | linux_release_core | `audio_rates` |
| LR-185 | linux_release_core | `signal_view` |
| LR-186 | linux_release_core | `channel` |
| LR-187 | linux_release_core | `symbol_burst` |
| LR-188 | linux_release_core | `symbol_burst_transmit` |
| LR-189 | linux_release_core | `symbol_burst_receiver` |
| LR-190 | linux_release_core | `symbol_burst_live` |
| LR-191 | linux_release_core | `amplitude_axis_reference_compat` |
| LR-192 | linux_release_core | `amplitude_older_probability_compat` |
| LR-193 | linux_release_core | `amplitude_arrival_strategy` |
| LR-194 | linux_release_core | `amplitude_live_arrival` |
| LR-195 | linux_release_core | `amplitude_arrival_planning` |
| LR-196 | linux_release_core | `amplitude_arrival_detector` |
| LR-197 | linux_release_core | `amplitude_arrival_arbitration` |
| LR-198 | linux_release_core | `scheduled_owned_fft_window_filter` |
| LR-199 | linux_release_core | `scheduled_owned_moment_runtime` |
| LR-200 | linux_release_core | `scheduled_sparse_owned_spans` |
| LR-201 | linux_release_core | `scheduled_owned_moment_correction` |
| LR-202 | linux_release_core | `scheduled_owned_field_errors` |
| LR-203 | linux_release_core | `scheduled_sparse_carrier` |
| LR-204 | linux_release_core | `scheduled_fine_curves_65` |
| LR-205 | linux_release_core | `scheduled_fine_curves_disabled_policy` |
| LR-206 | linux_release_core | `temporal_fine_curve_borrow` |
| LR-207 | linux_release_core | `temporal_fine_curve_cached` |
| LR-208 | linux_release_core | `scheduled_fine_curves_cached` |
| LR-209 | linux_release_core | `component_avx2_portable` |
| LR-210 | linux_release_core | `gui_layout` |
| LR-211 | linux_release_core | `gui_contract` |
| LR-212 | linux_release_core | `gui_smoke_replay` |
| LR-213 | linux_release_core | `gui_smoke_budget` |
| LR-214 | linux_release_core | `rev_event_pump` |
| LR-215 | linux_release_core | `gui_link_boundary` |
| LR-216 | linux_release_core | `gui_document_layout` |
| LR-217 | linux_release_core | `gui_interactions` |
| LR-218 | linux_release_core | `gui_services` |
| LR-219 | linux_release_core | `gui_bindings` |
| LR-220 | linux_release_core | `gui_record_reconciliation` |
| LR-221 | linux_release_core | `gui_document_presentation` |
| LR-222 | linux_release_core | `gui_chrome` |
| LR-223 | linux_release_core | `gui_inspection` |
| LR-224 | linux_release_core | `gui_pattern_space` |
| LR-225 | linux_release_core | `gui_plots` |
| LR-226 | linux_release_core | `rev_utf8` |
| LR-227 | linux_release_core | `gui_bitmaps` |
| LR-228 | linux_release_core | `gui_inspection_page` |
| LR-229 | linux_release_core | `gui_binary_editor` |
| LR-230 | linux_release_core | `gui_timing_advice` |
| LR-231 | linux_release_core | `gui_scheduled_cold_start` |
| LR-232 | linux_release_core | `gui_controller` |
| LR-233 | linux_release_core | `gui_profile_reference` |
| LR-234 | linux_release_core | `gui_link_planner` |
| LR-235 | linux_release_core | `amplitude_planner_probability` |
| LR-236 | linux_release_core | `link_planner_curves` |
| LR-237 | linux_release_core | `gui_launch_command` |
| LR-238 | linux_release_core | `gui_launch_settings` |
| LR-239 | linux_release_core | `gui_application` |
| LR-240 | linux_release_core | `gui_overlay` |
| LR-241 | linux_release_core | `gui_adapter_boundary` |
| LR-242 | linux_release_core | `gui_boundary_regression` |
| LR-243 | linux_release_core | `gui_native_policy` |
| LR-244 | linux_release_core | `gui_self_check` |
| LR-245 | linux_release_core | `alsa_audio_contract` |
| LR-246 | linux_release_core | `alsa_interleave_playback` |
| LR-247 | linux_release_core | `windows_audio_contract` |
| LR-248 | linux_release_core | `live_planning_link` |
| LR-249 | linux_release_core | `packaging_support` |
| LR-250 | linux_release_core | `packaging_cxx_runtime` |
| LR-251 | linux_release_core | `native_relocation` |
| LR-252 | linux_release_core | `sdk_packaging` |
| LD-001 | linux_debug_core | `speculation` |
| LD-002 | linux_debug_core | `legacy_psk` |
| LD-003 | linux_debug_core | `legacy_olivia` |
| LD-004 | linux_debug_core | `legacy_session` |
| LD-005 | linux_debug_core | `legacy_simulation` |
| LD-006 | linux_debug_core | `legacy_boundary` |
| LD-007 | linux_debug_core | `gui_legacy` |
| LD-008 | linux_debug_core | `gui_legacy_live` |
| LD-009 | linux_debug_core | `gui_legacy_poll` |
| LD-010 | linux_debug_core | `vendored_lzma_source` |
| LD-011 | linux_debug_core | `scheduled_original_pulse_row` |
| LD-012 | linux_debug_core | `scheduled_exact_source_points` |
| LD-013 | linux_debug_core | `scheduled_cold_colored_source_rows` |
| LD-014 | linux_debug_core | `scheduled_workspace_accounting` |
| LD-015 | linux_debug_core | `scheduled_float_spectral_accumulator` |
| LD-016 | linux_debug_core | `scheduled_float_acquisition_tile` |
| LD-017 | linux_debug_core | `scheduled_cold_colored_runtime` |
| LD-018 | linux_debug_core | `scheduled_cold_colored_proxy_runtime` |
| LD-019 | linux_debug_core | `live_workspace_capacity` |
| LD-020 | linux_debug_core | `scheduled_acquisition_memory` |
| LD-021 | linux_debug_core | `scheduled_capture_layout` |
| LD-022 | linux_debug_core | `scheduled_carrier_restart` |
| LD-023 | linux_debug_core | `scheduled_cold_colored_certificate` |
| LD-024 | linux_debug_core | `scheduled_exact_source_samples` |
| LD-025 | linux_debug_core | `scheduled_frequency_postinverse` |
| LD-026 | linux_debug_core | `scheduled_lazy_acquisition` |
| LD-027 | linux_debug_core | `scheduled_memory_projection` |
| LD-028 | linux_debug_core | `scheduled_original_projection_cache` |
| LD-029 | linux_debug_core | `scheduled_processing_journal` |
| LD-030 | linux_debug_core | `scheduled_acquisition_tile` |
| LD-031 | linux_debug_core | `scheduled_physical_multidwell` |
| LD-032 | linux_debug_core | `scheduled_polynomial_chip_moments` |
| LD-033 | linux_debug_core | `scheduled_polynomial_chip_runtime` |
| LD-034 | linux_debug_core | `scheduled_polynomial_chip_fft` |
| LD-035 | linux_debug_core | `scheduled_integral_colored` |
| LD-036 | linux_debug_core | `scheduled_origin_gram` |
| LD-037 | linux_debug_core | `scheduled_origin_gram_family` |
| LD-038 | linux_debug_core | `scheduled_origin_gram_bulk` |
| LD-039 | linux_debug_core | `scheduled_zero_guard` |
| LD-040 | linux_debug_core | `scheduled_linear_native_bound` |
| LD-041 | linux_debug_core | `scheduled_linear_gain_cells` |
| LD-042 | linux_debug_core | `scheduled_linear_direct_runs` |
| LD-043 | linux_debug_core | `scheduled_linear_fragments` |
| LD-044 | linux_debug_core | `scheduled_default_linear_cellstats` |
| LD-045 | linux_debug_core | `scheduled_default_linear_centers` |
| LD-046 | linux_debug_core | `scheduled_default_linear_events` |
| LD-047 | linux_debug_core | `scheduled_default_linear_runtime` |
| LD-048 | linux_debug_core | `scheduled_default_linear_fhss` |
| LD-049 | linux_debug_core | `scheduled_default_point_fft` |
| LD-050 | linux_debug_core | `scheduled_default_point_observation` |
| LD-051 | linux_debug_core | `scheduled_default_point_fhss` |
| LD-052 | linux_debug_core | `scheduled_default_point_guard_archive` |
| LD-053 | linux_debug_core | `scheduled_default_point_guard_edges` |
| LD-054 | linux_debug_core | `scheduled_default_prepared_endstrips` |
| LD-055 | linux_debug_core | `scheduled_default_point_guard_archive_runtime` |
| LD-056 | linux_debug_core | `scheduled_default_prepared_events` |
| LD-057 | linux_debug_core | `scheduled_default_point_fft_runtime` |
| LD-058 | linux_debug_core | `scheduled_default_point_family` |
| LD-059 | linux_debug_core | `scheduled_default_point_template_cache` |
| LD-060 | linux_debug_core | `scheduled_default_point_template_family` |
| LD-061 | linux_debug_core | `scheduled_default_point_bulk` |
| LD-062 | linux_debug_core | `scheduled_default_point_guard_summary` |
| LD-063 | linux_debug_core | `scheduled_default_compact_chords` |
| LD-064 | linux_debug_core | `scheduled_default_point_compact` |
| LD-065 | linux_debug_core | `scheduled_default_point_reference_recipe` |
| LD-066 | linux_debug_core | `scheduled_point_guard_prepass` |
| LD-067 | linux_debug_core | `scheduled_default_guard_cohort` |
| LD-068 | linux_debug_core | `scheduled_point_source_nodes` |
| LD-069 | linux_debug_core | `scheduled_linear_family` |
| LD-070 | linux_debug_core | `scheduled_template_cpu_attribution` |
| LD-071 | linux_debug_core | `scheduled_fractional_basis` |
| LD-072 | linux_debug_core | `scheduled_pulse_chord_certificate` |
| LD-073 | linux_debug_core | `scheduled_gain_chord` |
| LD-074 | linux_debug_core | `scheduled_pulse_derivative_chord_certificate` |
| LD-075 | linux_debug_core | `scheduled_gain_derivative_hook` |
| LD-076 | linux_debug_core | `scheduled_fractional_basis_runtime` |
| LD-077 | linux_debug_core | `scheduled_cold_owned_proxy` |
| LD-078 | linux_debug_core | `scheduled_colored_lattice` |
| LD-079 | linux_debug_core | `scheduled_partial_colored` |
| LD-080 | linux_debug_core | `scheduled_partial_colored_runtime` |
| LD-081 | linux_debug_core | `scheduled_residual_prefix` |
| LD-082 | linux_debug_core | `scheduled_acquisition_catalog` |
| LD-083 | linux_debug_core | `scheduled_acquisition_quarters` |
| LD-084 | linux_debug_core | `shared_scheduled_capture` |
| LD-085 | linux_debug_core | `scheduled_certificate_diagnostics` |
| LD-086 | linux_debug_core | `scheduled_cancellation_resume` |
| LD-087 | linux_debug_core | `scheduled_template_proxy_certificate` |
| LD-088 | linux_debug_core | `scheduled_diagnostic_journal` |
| LD-089 | linux_debug_core | `scheduled_parallel_preparation` |
| LD-090 | linux_debug_core | `scheduled_frequency_piece` |
| LD-091 | linux_debug_core | `scheduled_private_batch` |
| LD-092 | linux_debug_core | `scheduled_detached_replay` |
| LD-093 | linux_debug_core | `scheduled_detached_resume` |
| LD-094 | linux_debug_core | `scheduled_fine_curves` |
| LD-095 | linux_debug_core | `component_avx2` |
| LD-096 | linux_debug_core | `outer_public_replay` |
| LD-097 | linux_debug_core | `temporal_entry_payload` |
| LD-098 | linux_debug_core | `fp32_packed_l1` |
| LD-099 | linux_debug_core | `scheduled_r1_export` |
| LD-100 | linux_debug_core | `scheduled_flat_export` |
| LD-101 | linux_debug_core | `temporal_outer_plan` |
| LD-102 | linux_debug_core | `temporal_fp32_adapter` |
| LD-103 | linux_debug_core | `cached_pulse_count` |
| LD-104 | linux_debug_core | `pattern_coarse_cache` |
| LD-105 | linux_debug_core | `shared_temporal_plans` |
| LD-106 | linux_debug_core | `certified_ordered_plan` |
| LD-107 | linux_debug_core | `temporal_plan_cache` |
| LD-108 | linux_debug_core | `borrowed_plan_prepare` |
| LD-109 | linux_debug_core | `temporal_pool_consumer` |
| LD-110 | linux_debug_core | `temporal_normalization` |
| LD-111 | linux_debug_core | `borrowed_ordered_plan` |
| LD-112 | linux_debug_core | `temporal_templates` |
| LD-113 | linux_debug_core | `fine_cache_preservation` |
| LD-114 | linux_debug_core | `rx_cpu_profile` |
| LD-115 | linux_debug_core | `timing_continuation` |
| LD-116 | linux_debug_core | `pattern_startup_guide` |
| LD-117 | linux_debug_core | `physical_moment_cache` |
| LD-118 | linux_debug_core | `pattern_shared_source_curve` |
| LD-119 | linux_debug_core | `pattern_permutation_integration` |
| LD-120 | linux_debug_core | `scheduled_pulse_moments` |
| LD-121 | linux_debug_core | `scheduled_float_signal_moments` |
| LD-122 | linux_debug_core | `scheduled_float_gram` |
| LD-123 | linux_debug_core | `gram_override` |
| LD-124 | linux_debug_core | `scheduled_mapped_piece_groups` |
| LD-125 | linux_debug_core | `sched_hop` |
| LD-126 | linux_debug_core | `scheduled_symbol_calendar` |
| LD-127 | linux_debug_core | `sched_hop_transmit` |
| LD-128 | linux_debug_core | `sched_hop_receiver` |
| LD-129 | linux_debug_core | `sched_hop_complex` |
| LD-130 | linux_debug_core | `sched_hop_correlator` |
| LD-131 | linux_debug_core | `scheduled_complex_search` |
| LD-132 | linux_debug_core | `scheduled_frequency_fusion` |
| LD-133 | linux_debug_core | `sched_capture` |
| LD-134 | linux_debug_core | `crypto` |
| LD-135 | linux_debug_core | `keyring` |
| LD-136 | linux_debug_core | `stream_codec` |
| LD-137 | linux_debug_core | `recovery` |
| LD-138 | linux_debug_core | `attachment` |
| LD-139 | linux_debug_core | `stream_receive` |
| LD-140 | linux_debug_core | `boundary_sync` |
| LD-141 | linux_debug_core | `compression_short` |
| LD-142 | linux_debug_core | `compression_lzma` |
| LD-143 | linux_debug_core | `modem` |
| LD-144 | linux_debug_core | `streaming_modem` |
| LD-145 | linux_debug_core | `transmit_cover` |
| LD-146 | linux_debug_core | `noise_receive` |
| LD-147 | linux_debug_core | `pattern_code` |
| LD-148 | linux_debug_core | `pattern_receiver` |
| LD-149 | linux_debug_core | `pattern_correlator` |
| LD-150 | linux_debug_core | `pattern_drift` |
| LD-151 | linux_debug_core | `pattern_differential` |
| LD-152 | linux_debug_core | `receiver_differential` |
| LD-153 | linux_debug_core | `pattern_fft_batch` |
| LD-154 | linux_debug_core | `pattern_correlator_batch` |
| LD-155 | linux_debug_core | `pattern_projection_cache` |
| LD-156 | linux_debug_core | `pattern_envelope` |
| LD-157 | linux_debug_core | `receiver_strategy` |
| LD-158 | linux_debug_core | `pattern_search` |
| LD-159 | linux_debug_core | `pattern_start_geometry` |
| LD-160 | linux_debug_core | `search_parallel` |
| LD-161 | linux_debug_core | `search_arithmetic` |
| LD-162 | linux_debug_core | `search_fft` |
| LD-163 | linux_debug_core | `pulse_shaping` |
| LD-164 | linux_debug_core | `pattern_transfer` |
| LD-165 | linux_debug_core | `regressions` |
| LD-166 | linux_debug_core | `runtime` |
| LD-167 | linux_debug_core | `qr` |
| LD-168 | linux_debug_core | `transfer` |
| LD-169 | linux_debug_core | `tuning` |
| LD-170 | linux_debug_core | `simulation_estimate` |
| LD-171 | linux_debug_core | `receiver_probability` |
| LD-172 | linux_debug_core | `frequency_axis_probability` |
| LD-173 | linux_debug_core | `amplitude_probability` |
| LD-174 | linux_debug_core | `differential_probability` |
| LD-175 | linux_debug_core | `lpi_estimate` |
| LD-176 | linux_debug_core | `correlation_experiment` |
| LD-177 | linux_debug_core | `weak_signal` |
| LD-178 | linux_debug_core | `live_resources` |
| LD-179 | linux_debug_core | `live_receptions` |
| LD-180 | linux_debug_core | `live_transmit_lock` |
| LD-181 | linux_debug_core | `resampler` |
| LD-182 | linux_debug_core | `clock_sync` |
| LD-183 | linux_debug_core | `audio_provider_timing` |
| LD-184 | linux_debug_core | `audio_rates` |
| LD-185 | linux_debug_core | `signal_view` |
| LD-186 | linux_debug_core | `channel` |
| LD-187 | linux_debug_core | `symbol_burst` |
| LD-188 | linux_debug_core | `symbol_burst_transmit` |
| LD-189 | linux_debug_core | `symbol_burst_receiver` |
| LD-190 | linux_debug_core | `symbol_burst_live` |
| LD-191 | linux_debug_core | `amplitude_axis_reference_compat` |
| LD-192 | linux_debug_core | `amplitude_older_probability_compat` |
| LD-193 | linux_debug_core | `amplitude_arrival_strategy` |
| LD-194 | linux_debug_core | `amplitude_live_arrival` |
| LD-195 | linux_debug_core | `amplitude_arrival_planning` |
| LD-196 | linux_debug_core | `amplitude_arrival_detector` |
| LD-197 | linux_debug_core | `amplitude_arrival_arbitration` |
| LD-198 | linux_debug_core | `scheduled_owned_fft_window_filter` |
| LD-199 | linux_debug_core | `scheduled_owned_moment_runtime` |
| LD-200 | linux_debug_core | `scheduled_sparse_owned_spans` |
| LD-201 | linux_debug_core | `scheduled_owned_moment_correction` |
| LD-202 | linux_debug_core | `scheduled_owned_field_errors` |
| LD-203 | linux_debug_core | `scheduled_sparse_carrier` |
| LD-204 | linux_debug_core | `scheduled_fine_curves_65` |
| LD-205 | linux_debug_core | `scheduled_fine_curves_disabled_policy` |
| LD-206 | linux_debug_core | `temporal_fine_curve_borrow` |
| LD-207 | linux_debug_core | `temporal_fine_curve_cached` |
| LD-208 | linux_debug_core | `scheduled_fine_curves_cached` |
| LD-209 | linux_debug_core | `component_avx2_portable` |
| LD-210 | linux_debug_core | `gui_layout` |
| LD-211 | linux_debug_core | `gui_contract` |
| LD-212 | linux_debug_core | `gui_smoke_replay` |
| LD-213 | linux_debug_core | `gui_smoke_budget` |
| LD-214 | linux_debug_core | `rev_event_pump` |
| LD-215 | linux_debug_core | `gui_link_boundary` |
| LD-216 | linux_debug_core | `gui_document_layout` |
| LD-217 | linux_debug_core | `gui_interactions` |
| LD-218 | linux_debug_core | `gui_services` |
| LD-219 | linux_debug_core | `gui_bindings` |
| LD-220 | linux_debug_core | `gui_record_reconciliation` |
| LD-221 | linux_debug_core | `gui_document_presentation` |
| LD-222 | linux_debug_core | `gui_chrome` |
| LD-223 | linux_debug_core | `gui_inspection` |
| LD-224 | linux_debug_core | `gui_pattern_space` |
| LD-225 | linux_debug_core | `gui_plots` |
| LD-226 | linux_debug_core | `rev_utf8` |
| LD-227 | linux_debug_core | `gui_bitmaps` |
| LD-228 | linux_debug_core | `gui_inspection_page` |
| LD-229 | linux_debug_core | `gui_binary_editor` |
| LD-230 | linux_debug_core | `gui_timing_advice` |
| LD-231 | linux_debug_core | `gui_scheduled_cold_start` |
| LD-232 | linux_debug_core | `gui_controller` |
| LD-233 | linux_debug_core | `gui_profile_reference` |
| LD-234 | linux_debug_core | `gui_link_planner` |
| LD-235 | linux_debug_core | `amplitude_planner_probability` |
| LD-236 | linux_debug_core | `link_planner_curves` |
| LD-237 | linux_debug_core | `gui_launch_command` |
| LD-238 | linux_debug_core | `gui_launch_settings` |
| LD-239 | linux_debug_core | `gui_application` |
| LD-240 | linux_debug_core | `gui_overlay` |
| LD-241 | linux_debug_core | `gui_adapter_boundary` |
| LD-242 | linux_debug_core | `gui_boundary_regression` |
| LD-243 | linux_debug_core | `gui_native_policy` |
| LD-244 | linux_debug_core | `gui_self_check` |
| LD-245 | linux_debug_core | `alsa_audio_contract` |
| LD-246 | linux_debug_core | `alsa_interleave_playback` |
| LD-247 | linux_debug_core | `windows_audio_contract` |
| LD-248 | linux_debug_core | `live_planning_link` |
| LD-249 | linux_debug_core | `sdk_packaging` |
| WR-001 | windows_release_core | `speculation` |
| WR-002 | windows_release_core | `legacy_psk` |
| WR-003 | windows_release_core | `legacy_olivia` |
| WR-004 | windows_release_core | `legacy_session` |
| WR-005 | windows_release_core | `legacy_simulation` |
| WR-006 | windows_release_core | `legacy_boundary` |
| WR-007 | windows_release_core | `gui_legacy` |
| WR-008 | windows_release_core | `gui_legacy_live` |
| WR-009 | windows_release_core | `gui_legacy_poll` |
| WR-010 | windows_release_core | `vendored_lzma_source` |
| WR-011 | windows_release_core | `scheduled_original_pulse_row` |
| WR-012 | windows_release_core | `scheduled_cold_colored_source_rows` |
| WR-013 | windows_release_core | `scheduled_workspace_accounting` |
| WR-014 | windows_release_core | `scheduled_float_spectral_accumulator` |
| WR-015 | windows_release_core | `scheduled_float_acquisition_tile` |
| WR-016 | windows_release_core | `scheduled_cold_colored_runtime` |
| WR-017 | windows_release_core | `scheduled_cold_colored_proxy_runtime` |
| WR-018 | windows_release_core | `live_workspace_capacity` |
| WR-019 | windows_release_core | `scheduled_acquisition_memory` |
| WR-020 | windows_release_core | `scheduled_capture_layout` |
| WR-021 | windows_release_core | `scheduled_carrier_restart` |
| WR-022 | windows_release_core | `scheduled_cold_colored_certificate` |
| WR-023 | windows_release_core | `scheduled_exact_source_samples` |
| WR-024 | windows_release_core | `scheduled_frequency_postinverse` |
| WR-025 | windows_release_core | `scheduled_lazy_acquisition` |
| WR-026 | windows_release_core | `scheduled_memory_projection` |
| WR-027 | windows_release_core | `scheduled_original_projection_cache` |
| WR-028 | windows_release_core | `scheduled_processing_journal` |
| WR-029 | windows_release_core | `scheduled_acquisition_tile` |
| WR-030 | windows_release_core | `scheduled_physical_multidwell` |
| WR-031 | windows_release_core | `scheduled_polynomial_chip_moments` |
| WR-032 | windows_release_core | `scheduled_polynomial_chip_runtime` |
| WR-033 | windows_release_core | `scheduled_polynomial_chip_fft` |
| WR-034 | windows_release_core | `scheduled_integral_colored` |
| WR-035 | windows_release_core | `scheduled_origin_gram` |
| WR-036 | windows_release_core | `scheduled_origin_gram_family` |
| WR-037 | windows_release_core | `scheduled_origin_gram_bulk` |
| WR-038 | windows_release_core | `scheduled_linear_native_bound` |
| WR-039 | windows_release_core | `scheduled_linear_gain_cells` |
| WR-040 | windows_release_core | `scheduled_linear_direct_runs` |
| WR-041 | windows_release_core | `scheduled_linear_fragments` |
| WR-042 | windows_release_core | `scheduled_default_linear_cellstats` |
| WR-043 | windows_release_core | `scheduled_default_linear_centers` |
| WR-044 | windows_release_core | `scheduled_default_linear_events` |
| WR-045 | windows_release_core | `scheduled_default_linear_runtime` |
| WR-046 | windows_release_core | `scheduled_default_point_fft` |
| WR-047 | windows_release_core | `scheduled_default_point_observation` |
| WR-048 | windows_release_core | `scheduled_default_point_guard_archive` |
| WR-049 | windows_release_core | `scheduled_default_point_guard_edges` |
| WR-050 | windows_release_core | `scheduled_default_prepared_endstrips` |
| WR-051 | windows_release_core | `scheduled_default_point_guard_archive_runtime` |
| WR-052 | windows_release_core | `scheduled_default_prepared_events` |
| WR-053 | windows_release_core | `scheduled_default_point_fft_runtime` |
| WR-054 | windows_release_core | `scheduled_default_point_family` |
| WR-055 | windows_release_core | `scheduled_default_point_template_cache` |
| WR-056 | windows_release_core | `scheduled_default_point_template_family` |
| WR-057 | windows_release_core | `scheduled_default_point_bulk` |
| WR-058 | windows_release_core | `scheduled_default_point_guard_summary` |
| WR-059 | windows_release_core | `scheduled_default_compact_chords` |
| WR-060 | windows_release_core | `scheduled_default_point_compact` |
| WR-061 | windows_release_core | `scheduled_point_guard_prepass` |
| WR-062 | windows_release_core | `scheduled_default_guard_cohort` |
| WR-063 | windows_release_core | `scheduled_point_source_nodes` |
| WR-064 | windows_release_core | `scheduled_linear_family` |
| WR-065 | windows_release_core | `scheduled_template_cpu_attribution` |
| WR-066 | windows_release_core | `scheduled_fractional_basis` |
| WR-067 | windows_release_core | `scheduled_pulse_chord_certificate` |
| WR-068 | windows_release_core | `scheduled_pulse_derivative_chord_certificate` |
| WR-069 | windows_release_core | `scheduled_fractional_basis_runtime` |
| WR-070 | windows_release_core | `scheduled_cold_owned_proxy` |
| WR-071 | windows_release_core | `scheduled_partial_colored` |
| WR-072 | windows_release_core | `scheduled_partial_colored_runtime` |
| WR-073 | windows_release_core | `scheduled_residual_prefix` |
| WR-074 | windows_release_core | `scheduled_acquisition_catalog` |
| WR-075 | windows_release_core | `scheduled_acquisition_quarters` |
| WR-076 | windows_release_core | `shared_scheduled_capture` |
| WR-077 | windows_release_core | `scheduled_certificate_diagnostics` |
| WR-078 | windows_release_core | `scheduled_cancellation_resume` |
| WR-079 | windows_release_core | `scheduled_template_proxy_certificate` |
| WR-080 | windows_release_core | `scheduled_diagnostic_journal` |
| WR-081 | windows_release_core | `scheduled_parallel_preparation` |
| WR-082 | windows_release_core | `scheduled_frequency_piece` |
| WR-083 | windows_release_core | `scheduled_private_batch` |
| WR-084 | windows_release_core | `scheduled_detached_replay` |
| WR-085 | windows_release_core | `scheduled_detached_resume` |
| WR-086 | windows_release_core | `scheduled_fine_curves` |
| WR-087 | windows_release_core | `outer_public_replay` |
| WR-088 | windows_release_core | `scheduled_r1_export` |
| WR-089 | windows_release_core | `scheduled_flat_export` |
| WR-090 | windows_release_core | `cached_pulse_count` |
| WR-091 | windows_release_core | `pattern_coarse_cache` |
| WR-092 | windows_release_core | `shared_temporal_plans` |
| WR-093 | windows_release_core | `certified_ordered_plan` |
| WR-094 | windows_release_core | `temporal_plan_cache` |
| WR-095 | windows_release_core | `borrowed_plan_prepare` |
| WR-096 | windows_release_core | `temporal_pool_consumer` |
| WR-097 | windows_release_core | `temporal_normalization` |
| WR-098 | windows_release_core | `borrowed_ordered_plan` |
| WR-099 | windows_release_core | `temporal_templates` |
| WR-100 | windows_release_core | `fine_cache_preservation` |
| WR-101 | windows_release_core | `rx_cpu_profile` |
| WR-102 | windows_release_core | `timing_continuation` |
| WR-103 | windows_release_core | `pattern_startup_guide` |
| WR-104 | windows_release_core | `physical_moment_cache` |
| WR-105 | windows_release_core | `pattern_shared_source_curve` |
| WR-106 | windows_release_core | `pattern_permutation_integration` |
| WR-107 | windows_release_core | `scheduled_pulse_moments` |
| WR-108 | windows_release_core | `scheduled_float_signal_moments` |
| WR-109 | windows_release_core | `scheduled_float_gram` |
| WR-110 | windows_release_core | `gram_override` |
| WR-111 | windows_release_core | `scheduled_mapped_piece_groups` |
| WR-112 | windows_release_core | `sched_hop` |
| WR-113 | windows_release_core | `scheduled_symbol_calendar` |
| WR-114 | windows_release_core | `sched_hop_transmit` |
| WR-115 | windows_release_core | `sched_hop_receiver` |
| WR-116 | windows_release_core | `sched_hop_complex` |
| WR-117 | windows_release_core | `sched_hop_correlator` |
| WR-118 | windows_release_core | `scheduled_complex_search` |
| WR-119 | windows_release_core | `scheduled_frequency_fusion` |
| WR-120 | windows_release_core | `sched_capture` |
| WR-121 | windows_release_core | `crypto` |
| WR-122 | windows_release_core | `keyring` |
| WR-123 | windows_release_core | `stream_codec` |
| WR-124 | windows_release_core | `recovery` |
| WR-125 | windows_release_core | `attachment` |
| WR-126 | windows_release_core | `stream_receive` |
| WR-127 | windows_release_core | `boundary_sync` |
| WR-128 | windows_release_core | `compression_short` |
| WR-129 | windows_release_core | `compression_lzma` |
| WR-130 | windows_release_core | `modem` |
| WR-131 | windows_release_core | `streaming_modem` |
| WR-132 | windows_release_core | `transmit_cover` |
| WR-133 | windows_release_core | `noise_receive` |
| WR-134 | windows_release_core | `pattern_code` |
| WR-135 | windows_release_core | `pattern_receiver` |
| WR-136 | windows_release_core | `pattern_correlator` |
| WR-137 | windows_release_core | `pattern_drift` |
| WR-138 | windows_release_core | `pattern_differential` |
| WR-139 | windows_release_core | `receiver_differential` |
| WR-140 | windows_release_core | `pattern_fft_batch` |
| WR-141 | windows_release_core | `pattern_correlator_batch` |
| WR-142 | windows_release_core | `pattern_projection_cache` |
| WR-143 | windows_release_core | `pattern_envelope` |
| WR-144 | windows_release_core | `receiver_strategy` |
| WR-145 | windows_release_core | `pattern_search` |
| WR-146 | windows_release_core | `pattern_start_geometry` |
| WR-147 | windows_release_core | `search_parallel` |
| WR-148 | windows_release_core | `search_arithmetic` |
| WR-149 | windows_release_core | `search_fft` |
| WR-150 | windows_release_core | `pulse_shaping` |
| WR-151 | windows_release_core | `pattern_transfer` |
| WR-152 | windows_release_core | `regressions` |
| WR-153 | windows_release_core | `runtime` |
| WR-154 | windows_release_core | `qr` |
| WR-155 | windows_release_core | `transfer` |
| WR-156 | windows_release_core | `tuning` |
| WR-157 | windows_release_core | `simulation_estimate` |
| WR-158 | windows_release_core | `receiver_probability` |
| WR-159 | windows_release_core | `frequency_axis_probability` |
| WR-160 | windows_release_core | `amplitude_probability` |
| WR-161 | windows_release_core | `differential_probability` |
| WR-162 | windows_release_core | `lpi_estimate` |
| WR-163 | windows_release_core | `correlation_experiment` |
| WR-164 | windows_release_core | `weak_signal` |
| WR-165 | windows_release_core | `live_resources` |
| WR-166 | windows_release_core | `live_receptions` |
| WR-167 | windows_release_core | `live_transmit_lock` |
| WR-168 | windows_release_core | `resampler` |
| WR-169 | windows_release_core | `clock_sync` |
| WR-170 | windows_release_core | `audio_provider_timing` |
| WR-171 | windows_release_core | `audio_rates` |
| WR-172 | windows_release_core | `signal_view` |
| WR-173 | windows_release_core | `channel` |
| WR-174 | windows_release_core | `symbol_burst` |
| WR-175 | windows_release_core | `symbol_burst_transmit` |
| WR-176 | windows_release_core | `symbol_burst_receiver` |
| WR-177 | windows_release_core | `symbol_burst_live` |
| WR-178 | windows_release_core | `amplitude_axis_reference_compat` |
| WR-179 | windows_release_core | `amplitude_older_probability_compat` |
| WR-180 | windows_release_core | `amplitude_arrival_strategy` |
| WR-181 | windows_release_core | `amplitude_live_arrival` |
| WR-182 | windows_release_core | `amplitude_arrival_planning` |
| WR-183 | windows_release_core | `amplitude_arrival_detector` |
| WR-184 | windows_release_core | `amplitude_arrival_arbitration` |
| WR-185 | windows_release_core | `scheduled_owned_fft_window_filter` |
| WR-186 | windows_release_core | `scheduled_owned_moment_correction` |
| WR-187 | windows_release_core | `scheduled_owned_field_errors` |
| WR-188 | windows_release_core | `scheduled_sparse_carrier` |
| WR-189 | windows_release_core | `scheduled_fine_curves_65` |
| WR-190 | windows_release_core | `scheduled_fine_curves_disabled_policy` |
| WR-191 | windows_release_core | `temporal_fine_curve_borrow` |
| WR-192 | windows_release_core | `temporal_fine_curve_cached` |
| WR-193 | windows_release_core | `scheduled_fine_curves_cached` |
| WR-194 | windows_release_core | `gui_layout` |
| WR-195 | windows_release_core | `gui_contract` |
| WR-196 | windows_release_core | `gui_smoke_replay` |
| WR-197 | windows_release_core | `gui_smoke_budget` |
| WR-198 | windows_release_core | `rev_event_pump` |
| WR-199 | windows_release_core | `gui_link_boundary` |
| WR-200 | windows_release_core | `gui_document_layout` |
| WR-201 | windows_release_core | `gui_interactions` |
| WR-202 | windows_release_core | `gui_services` |
| WR-203 | windows_release_core | `gui_bindings` |
| WR-204 | windows_release_core | `gui_record_reconciliation` |
| WR-205 | windows_release_core | `gui_document_presentation` |
| WR-206 | windows_release_core | `gui_chrome` |
| WR-207 | windows_release_core | `gui_inspection` |
| WR-208 | windows_release_core | `gui_pattern_space` |
| WR-209 | windows_release_core | `gui_plots` |
| WR-210 | windows_release_core | `rev_utf8` |
| WR-211 | windows_release_core | `gui_bitmaps` |
| WR-212 | windows_release_core | `gui_inspection_page` |
| WR-213 | windows_release_core | `gui_binary_editor` |
| WR-214 | windows_release_core | `gui_timing_advice` |
| WR-215 | windows_release_core | `gui_scheduled_cold_start` |
| WR-216 | windows_release_core | `gui_controller` |
| WR-217 | windows_release_core | `gui_profile_reference` |
| WR-218 | windows_release_core | `gui_link_planner` |
| WR-219 | windows_release_core | `amplitude_planner_probability` |
| WR-220 | windows_release_core | `link_planner_curves` |
| WR-221 | windows_release_core | `gui_launch_command` |
| WR-222 | windows_release_core | `gui_launch_settings` |
| WR-223 | windows_release_core | `gui_application` |
| WR-224 | windows_release_core | `gui_overlay` |
| WR-225 | windows_release_core | `gui_adapter_boundary` |
| WR-226 | windows_release_core | `gui_boundary_regression` |
| WR-227 | windows_release_core | `gui_native_policy` |
| WR-228 | windows_release_core | `gui_self_check` |
| WR-229 | windows_release_core | `live_planning_link` |
| WR-230 | windows_release_core | `packaging_support` |
| WR-231 | windows_release_core | `native_relocation` |
| SG-001 | sdk_fltk_contract | `scheduled_native_cohort` |
| SG-002 | sdk_fltk_contract | `scheduled_native_coherent_prefit` |
| SG-003 | sdk_fltk_contract | `scheduled_original_pulse_row` |
| SG-004 | sdk_fltk_contract | `scheduled_exact_source_points` |
| SG-005 | sdk_fltk_contract | `scheduled_cold_colored_source_rows` |
| SG-006 | sdk_fltk_contract | `scheduled_workspace_accounting` |
| SG-007 | sdk_fltk_contract | `scheduled_float_spectral_accumulator` |
| SG-008 | sdk_fltk_contract | `scheduled_float_acquisition_tile` |
| SG-009 | sdk_fltk_contract | `scheduled_cold_colored_runtime` |
| SG-010 | sdk_fltk_contract | `scheduled_cold_colored_proxy_runtime` |
| SG-011 | sdk_fltk_contract | `live_workspace_capacity` |
| SG-012 | sdk_fltk_contract | `scheduled_acquisition_memory` |
| SG-013 | sdk_fltk_contract | `scheduled_capture_layout` |
| SG-014 | sdk_fltk_contract | `scheduled_carrier_restart` |
| SG-015 | sdk_fltk_contract | `scheduled_cold_colored_certificate` |
| SG-016 | sdk_fltk_contract | `scheduled_exact_source_samples` |
| SG-017 | sdk_fltk_contract | `scheduled_frequency_postinverse` |
| SG-018 | sdk_fltk_contract | `scheduled_lazy_acquisition` |
| SG-019 | sdk_fltk_contract | `scheduled_memory_projection` |
| SG-020 | sdk_fltk_contract | `scheduled_original_projection_cache` |
| SG-021 | sdk_fltk_contract | `scheduled_processing_journal` |
| SG-022 | sdk_fltk_contract | `scheduled_acquisition_tile` |
| SG-023 | sdk_fltk_contract | `scheduled_physical_multidwell` |
| SG-024 | sdk_fltk_contract | `scheduled_polynomial_chip_moments` |
| SG-025 | sdk_fltk_contract | `scheduled_polynomial_chip_runtime` |
| SG-026 | sdk_fltk_contract | `scheduled_polynomial_chip_fft` |
| SG-027 | sdk_fltk_contract | `scheduled_integral_colored` |
| SG-028 | sdk_fltk_contract | `scheduled_origin_gram` |
| SG-029 | sdk_fltk_contract | `scheduled_origin_gram_family` |
| SG-030 | sdk_fltk_contract | `scheduled_origin_gram_bulk` |
| SG-031 | sdk_fltk_contract | `scheduled_zero_guard` |
| SG-032 | sdk_fltk_contract | `scheduled_linear_native_bound` |
| SG-033 | sdk_fltk_contract | `scheduled_linear_gain_cells` |
| SG-034 | sdk_fltk_contract | `scheduled_linear_direct_runs` |
| SG-035 | sdk_fltk_contract | `scheduled_linear_fragments` |
| SG-036 | sdk_fltk_contract | `scheduled_default_linear_cellstats` |
| SG-037 | sdk_fltk_contract | `scheduled_default_linear_centers` |
| SG-038 | sdk_fltk_contract | `scheduled_default_linear_events` |
| SG-039 | sdk_fltk_contract | `scheduled_default_linear_runtime` |
| SG-040 | sdk_fltk_contract | `scheduled_default_linear_fhss` |
| SG-041 | sdk_fltk_contract | `scheduled_default_linear_fhss_original` |
| SG-042 | sdk_fltk_contract | `scheduled_default_linear_original` |
| SG-043 | sdk_fltk_contract | `scheduled_default_point_fft` |
| SG-044 | sdk_fltk_contract | `scheduled_default_point_observation` |
| SG-045 | sdk_fltk_contract | `scheduled_default_point_fhss` |
| SG-046 | sdk_fltk_contract | `scheduled_default_point_guard_archive` |
| SG-047 | sdk_fltk_contract | `scheduled_default_point_guard_edges` |
| SG-048 | sdk_fltk_contract | `scheduled_default_prepared_endstrips` |
| SG-049 | sdk_fltk_contract | `scheduled_default_point_guard_edges_runtime` |
| SG-050 | sdk_fltk_contract | `scheduled_default_point_guard_archive_runtime` |
| SG-051 | sdk_fltk_contract | `scheduled_default_prepared_events` |
| SG-052 | sdk_fltk_contract | `scheduled_default_point_fft_runtime` |
| SG-053 | sdk_fltk_contract | `scheduled_default_point_family` |
| SG-054 | sdk_fltk_contract | `scheduled_default_point_template_cache` |
| SG-055 | sdk_fltk_contract | `scheduled_default_point_template_family` |
| SG-056 | sdk_fltk_contract | `scheduled_default_point_bulk` |
| SG-057 | sdk_fltk_contract | `scheduled_default_point_bulk_original` |
| SG-058 | sdk_fltk_contract | `scheduled_default_point_guard_summary` |
| SG-059 | sdk_fltk_contract | `scheduled_default_compact_chords` |
| SG-060 | sdk_fltk_contract | `scheduled_default_point_compact` |
| SG-061 | sdk_fltk_contract | `scheduled_default_point_reference_recipe` |
| SG-062 | sdk_fltk_contract | `scheduled_default_point_compact_runtime` |
| SG-063 | sdk_fltk_contract | `scheduled_point_guard_prepass` |
| SG-064 | sdk_fltk_contract | `scheduled_point_guard_prepass_runtime` |
| SG-065 | sdk_fltk_contract | `scheduled_default_point_diagnostics` |
| SG-066 | sdk_fltk_contract | `scheduled_default_guard_cohort` |
| SG-067 | sdk_fltk_contract | `scheduled_point_source_nodes` |
| SG-068 | sdk_fltk_contract | `scheduled_linear_family` |
| SG-069 | sdk_fltk_contract | `scheduled_linear_grid_original` |
| SG-070 | sdk_fltk_contract | `scheduled_template_cpu_attribution` |
| SG-071 | sdk_fltk_contract | `scheduled_fractional_basis` |
| SG-072 | sdk_fltk_contract | `scheduled_pulse_chord_certificate` |
| SG-073 | sdk_fltk_contract | `scheduled_gain_chord` |
| SG-074 | sdk_fltk_contract | `scheduled_pulse_derivative_chord_certificate` |
| SG-075 | sdk_fltk_contract | `scheduled_gain_derivative_hook` |
| SG-076 | sdk_fltk_contract | `scheduled_fractional_basis_runtime` |
| SG-077 | sdk_fltk_contract | `scheduled_fractional_basis_original` |
| SG-078 | sdk_fltk_contract | `scheduled_origin_gram_original` |
| SG-079 | sdk_fltk_contract | `scheduled_cold_owned_proxy` |
| SG-080 | sdk_fltk_contract | `scheduled_general_colored` |
| SG-081 | sdk_fltk_contract | `scheduled_colored_lattice` |
| SG-082 | sdk_fltk_contract | `scheduled_clock_reference` |
| SG-083 | sdk_fltk_contract | `scheduled_partial_colored` |
| SG-084 | sdk_fltk_contract | `scheduled_partial_colored_runtime` |
| SG-085 | sdk_fltk_contract | `scheduled_residual_prefix` |
| SG-086 | sdk_fltk_contract | `scheduled_acquisition_catalog` |
| SG-087 | sdk_fltk_contract | `scheduled_acquisition_quarters` |
| SG-088 | sdk_fltk_contract | `shared_scheduled_capture` |
| SG-089 | sdk_fltk_contract | `scheduled_certificate_diagnostics` |
| SG-090 | sdk_fltk_contract | `scheduled_cancellation_resume` |
| SG-091 | sdk_fltk_contract | `scheduled_template_proxy_certificate` |
| SG-092 | sdk_fltk_contract | `scheduled_diagnostic_journal` |
| SG-093 | sdk_fltk_contract | `scheduled_parallel_preparation` |
| SG-094 | sdk_fltk_contract | `scheduled_frequency_piece` |
| SG-095 | sdk_fltk_contract | `scheduled_private_batch` |
| SG-096 | sdk_fltk_contract | `scheduled_detached_replay` |
| SG-097 | sdk_fltk_contract | `scheduled_detached_resume` |
| SG-098 | sdk_fltk_contract | `scheduled_fine_curves` |
| SG-099 | sdk_fltk_contract | `component_avx2` |
| SG-100 | sdk_fltk_contract | `outer_public_replay` |
| SG-101 | sdk_fltk_contract | `temporal_entry_payload` |
| SG-102 | sdk_fltk_contract | `fp32_packed_l1` |
| SG-103 | sdk_fltk_contract | `scheduled_r1_export` |
| SG-104 | sdk_fltk_contract | `scheduled_flat_export` |
| SG-105 | sdk_fltk_contract | `temporal_outer_plan` |
| SG-106 | sdk_fltk_contract | `temporal_fp32_adapter` |
| SG-107 | sdk_fltk_contract | `cached_pulse_count` |
| SG-108 | sdk_fltk_contract | `pattern_coarse_cache` |
| SG-109 | sdk_fltk_contract | `shared_temporal_plans` |
| SG-110 | sdk_fltk_contract | `certified_ordered_plan` |
| SG-111 | sdk_fltk_contract | `temporal_plan_cache` |
| SG-112 | sdk_fltk_contract | `borrowed_plan_prepare` |
| SG-113 | sdk_fltk_contract | `temporal_pool_consumer` |
| SG-114 | sdk_fltk_contract | `temporal_normalization` |
| SG-115 | sdk_fltk_contract | `borrowed_ordered_plan` |
| SG-116 | sdk_fltk_contract | `temporal_templates` |
| SG-117 | sdk_fltk_contract | `fine_cache_preservation` |
| SG-118 | sdk_fltk_contract | `rx_cpu_profile` |
| SG-119 | sdk_fltk_contract | `timing_continuation` |
| SG-120 | sdk_fltk_contract | `pattern_startup_guide` |
| SG-121 | sdk_fltk_contract | `physical_moment_cache` |
| SG-122 | sdk_fltk_contract | `pattern_shared_source_curve` |
| SG-123 | sdk_fltk_contract | `pattern_permutation_integration` |
| SG-124 | sdk_fltk_contract | `scheduled_pulse_moments` |
| SG-125 | sdk_fltk_contract | `scheduled_float_signal_moments` |
| SG-126 | sdk_fltk_contract | `scheduled_float_gram` |
| SG-127 | sdk_fltk_contract | `gram_override` |
| SG-128 | sdk_fltk_contract | `scheduled_mapped_piece_groups` |
| SG-129 | sdk_fltk_contract | `sched_hop` |
| SG-130 | sdk_fltk_contract | `scheduled_symbol_calendar` |
| SG-131 | sdk_fltk_contract | `sched_hop_transmit` |
| SG-132 | sdk_fltk_contract | `sched_hop_receiver` |
| SG-133 | sdk_fltk_contract | `sched_hop_complex` |
| SG-134 | sdk_fltk_contract | `sched_hop_correlator` |
| SG-135 | sdk_fltk_contract | `scheduled_complex_search` |
| SG-136 | sdk_fltk_contract | `scheduled_frequency_fusion` |
| SG-137 | sdk_fltk_contract | `sched_capture` |
| SG-138 | sdk_fltk_contract | `stream_codec` |
| SG-139 | sdk_fltk_contract | `recovery` |
| SG-140 | sdk_fltk_contract | `attachment` |
| SG-141 | sdk_fltk_contract | `stream_receive` |
| SG-142 | sdk_fltk_contract | `compression_short` |
| SG-143 | sdk_fltk_contract | `pattern_receiver` |
| SG-144 | sdk_fltk_contract | `pattern_correlator` |
| SG-145 | sdk_fltk_contract | `pattern_drift` |
| SG-146 | sdk_fltk_contract | `pattern_differential` |
| SG-147 | sdk_fltk_contract | `receiver_differential` |
| SG-148 | sdk_fltk_contract | `pattern_fft_batch` |
| SG-149 | sdk_fltk_contract | `pattern_correlator_batch` |
| SG-150 | sdk_fltk_contract | `pattern_search` |
| SG-151 | sdk_fltk_contract | `pattern_start_geometry` |
| SG-152 | sdk_fltk_contract | `search_arithmetic` |
| SG-153 | sdk_fltk_contract | `search_fft` |
| SG-154 | sdk_fltk_contract | `transfer` |
| SG-155 | sdk_fltk_contract | `tuning` |
| SG-156 | sdk_fltk_contract | `simulation_estimate` |
| SG-157 | sdk_fltk_contract | `receiver_probability` |
| SG-158 | sdk_fltk_contract | `frequency_axis_probability` |
| SG-159 | sdk_fltk_contract | `amplitude_probability` |
| SG-160 | sdk_fltk_contract | `differential_probability` |
| SG-161 | sdk_fltk_contract | `weak_signal` |
| SG-162 | sdk_fltk_contract | `live_resources` |
| SG-163 | sdk_fltk_contract | `live_receptions` |
| SG-164 | sdk_fltk_contract | `live_transmit_lock` |
| SG-165 | sdk_fltk_contract | `clock_sync` |
| SG-166 | sdk_fltk_contract | `symbol_burst` |
| SG-167 | sdk_fltk_contract | `symbol_burst_transmit` |
| SG-168 | sdk_fltk_contract | `symbol_burst_receiver` |
| SG-169 | sdk_fltk_contract | `symbol_burst_live` |
| SG-170 | sdk_fltk_contract | `amplitude_arrival_strategy` |
| SG-171 | sdk_fltk_contract | `amplitude_live_arrival` |
| SG-172 | sdk_fltk_contract | `live_exact_source` |
| SG-173 | sdk_fltk_contract | `scheduled_owned_fft_window_filter` |
| SG-174 | sdk_fltk_contract | `scheduled_owned_fft_window_hook` |
| SG-175 | sdk_fltk_contract | `scheduled_owned_moment_runtime` |
| SG-176 | sdk_fltk_contract | `scheduled_owned_moments` |
| SG-177 | sdk_fltk_contract | `scheduled_sparse_owned_spans` |
| SG-178 | sdk_fltk_contract | `scheduled_owned_moment_correction` |
| SG-179 | sdk_fltk_contract | `scheduled_owned_field_errors` |
| SG-180 | sdk_fltk_contract | `scheduled_sparse_carrier` |
| SG-181 | sdk_fltk_contract | `scheduled_fine_curves_65` |
| SG-182 | sdk_fltk_contract | `scheduled_fine_curves_disabled_policy` |
| SG-183 | sdk_fltk_contract | `temporal_fine_curve_borrow` |
| SG-184 | sdk_fltk_contract | `temporal_fine_curve_cached` |
| SG-185 | sdk_fltk_contract | `scheduled_fine_curves_cached` |
| SG-186 | sdk_fltk_contract | `component_avx2_portable` |
| SG-187 | sdk_fltk_contract | `gui_inspection` |
| SG-188 | sdk_fltk_contract | `gui_binary_editor` |
| SG-189 | sdk_fltk_contract | `gui_timing_advice` |
| SG-190 | sdk_fltk_contract | `gui_scheduled_cold_start` |
| SG-191 | sdk_fltk_contract | `gui_controller` |
| SG-192 | sdk_fltk_contract | `gui_application` |
| SG-193 | sdk_fltk_contract | `cli` |
| SG-194 | sdk_fltk_contract | `live_planning_link` |
| SL-001 | sdk_fltk_contract | `live` |
| SL-002 | sdk_fltk_contract | `live_profiles` |
| SC-001 | sdk_fltk_contract | `differential_receiver_probability` |

## Earlier diagnostic checkpoint (historical, not a queue filter)

The following analysis was prepared before the batch capture repair and held
rescue fixture were committed in e231e4d. Its old HEAD/dirty/run status is
historical; reconcile newer main-chat evidence. It explains likely failure paths
and required ownership, but **does not limit or remove any of the 944 entries**.
The all 944 destination-run instruction above supersedes any earlier suggestion
to wait solely because work is in progress on the original computer. Real build,
platform and coupled timing dependencies still apply. Final canonical CI
qualification/release gates remain in their required order.

## Earlier issue-path analysis and ownership context

1. Read `/home/user/.codex/delegation-kit/policy.md` and its current
   `agents/luna_worker.toml`, `agents/luna_scout.toml`, and
   `agents/sol_expert.toml`; preserve the selected primary model/effort.
2. Read [AGENTS.md](AGENTS.md), [development contract](docs/development.md),
   [Symbol_Burst feature](docs/symbol-burst.md),
   [implementation plan](docs/symbol-burst-implementation-plan.md),
   [building/validation route](docs/building.md), and the **latest**
   [temporal-support continuation](.agent-work/notes/symbol-burst-temporal-support-current-20261020.md).
   Follow any superseding compact-note pointer before using older status.
3. Reconcile actual HEAD, dirty files, active native subagents/processes and CI.
   Reuse matching results; do not duplicate a run or overwrite active work.
   The original main chat may still be working. Establish one integration owner
   before overlapping writes; this document does not transfer ownership.
4. Carry forward the user override: **native subagent orchestration only**.
   Do not register sessions, acquire filesystem claims, scan coordination
   boards, maintain registries/inboxes/heartbeats, or migrate historical records.
   Preserve those records as evidence. Serialize shared Git/integration writes;
   give independent work disjoint paths and separate build/output spaces.

### Exact checkpoint and latest failure

The runtime correction is commit
`049e84edc1f8d3b6b69fef661680ff0e0100bc04`. The last observed working HEAD while
preparing this file was `a1b87253e257f0b2137c6c545587402dbd42fb87`; these are
**different identities**. `tests/test_pattern_correlator_batch.cpp` was dirty
with a main-chat repair. Re-read current status before editing or launching.

[Focused run 39487011213](https://github.com/mirage335-colossus/pumpModem/actions/runs/39487011213),
job `118849933416`, **failed compilation before any runtime test ran**:

```text
tests/test_pattern_correlator_batch.cpp:651:88: error: 'total' is not captured
```

The same diagnostic appears in copied logs; it is one distinct compiler error.
The original Sol owner has changed the nested lambda capture from `[]` to
`[&]`; this snapshot contains no successful build of that repair. Do not call
this a failed runtime prediction of the receiver correction. Read
[accepted compile-failure classification](.agent-work/artifacts/symbol-burst-postupdate-20261018/default-media-clock-implementation/production-qualification-a6c2bf1/temporal-support-focused-049e84e/root-review.json)
and the adjacent `readback/readback.md` before acting.

Original diagnosis/integration/acceptance owner:
**`instrumented-gui-core-failures-20261018` (Sol)**. Existing Sol takeovers stay
Sol-owned through completion, including `paced-burst-backlog-fixture-20261020`,
`paced-burst-ci-support-route`, `burst-fit-census-evidence-reader`, the
Correlator temporal-support work, and the temporal-support regression takeover.
Preserve all original failures, patches, deadlines and remaining repair limits.

## Enumerated open failures and unverified error paths

There is **one reproduced receiver functional defect**, **one observed compile
blocker**, and the specific unverified paths below. A coverage gap or pending
rerun is not itself a demonstrated error. No claim is made that this checklist
can enumerate unknown future defects.

### 1. New temporal-support test does not compile — observed error

- **Failure:** the batch regression's nested lambda uses `total` without a
  capture. No runtime acceptance check executed in run `39487011213`.
- **Source:** `tests/test_pattern_correlator_batch.cpp`,
  `temporal_support_tracks_observed_distribution()`.
- **Next/acceptance:** preserve and review the existing test-only capture repair;
  reconcile any newer run before launching. Compile the actual intended targets
  through the established workflow, then execute the unchanged acceptance route.
- **Ownership/parallelism:** existing Sol repair; do not dispatch a second Luna
  implementation of this same failed assignment. Independent evidence analysis
  for items 3–10 can proceed while compilation is pending.

### 2. Keyed Burst selects a longer profile and decodes the wrong final bits — reproduced defect

- **Observed baseline:** `7b79bc1`, run `39481116274`, job `118829644839`.
  All **223,296 PCM samples** were processed, queue drained, health clean and
  pacing valid. Final row was eight bits `01001001`, not the expected sixteen
  bits `0010010010010010`. This was not merely a provisional-prefix mismatch.
- **Located mechanism:** T8 fits used 1,024 physical / 128 native observations
  with R² about .993–.997. T16 fits used 2,048 / 256 with R² .418–.557, yet old
  support saturated at the full 2,048 samples. Blank portions therefore gained
  full duration credit during cross-profile selection.
- **Source:** `src/pattern_receiver.cpp`, `src/pattern_correlator.cpp`,
  `src/pattern_correlator_batch.hpp`; actual FFT regression
  `tests/test_pattern_receiver.cpp::burst_temporal_support()`.
- **Correction under test:** `049e84e` caps old complex non-differential Burst
  support using bounded local temporal fits. Native bit/admission/absence
  thresholds are retained. Its runtime prediction has **not yet been tested**.
- **Acceptance:** correct next-poll prefixes, identity and all sixteen final
  bits; complete physical-end observation; unchanged plaintext/keyed Off/On
  controls; no health, input, budget or source-tail failure. Compare the same
  PCM for baseline/follow instrumentation, including FNV `d34970be1abe7755`.
- **Evidence:**
  [accepted FFT census](.agent-work/artifacts/symbol-burst-postupdate-20261018/default-media-clock-implementation/production-qualification-a6c2bf1/paced-fft-fit-7b79bc1/root-review.json)
  and its `readback/` directory. Earlier Correlator-only trace hooks missed the
  actual FFT backend; reuse the corrected census rather than repeating that work.
- **Ownership:** Sol diagnosis and production integration. Luna may extract
  distinct portions of the same completed capture, not launch duplicate reproductions.

### 3. Independently paced post-startup backlog rescue — required behavior not demonstrated

- **Gap:** previous plaintext On cases were unchallenged; keyed Off accumulated
  debt without measured gap recovery; keyed On failed content. No run has yet
  demonstrated the required **at least three qualifying post-startup gap clears**.
- **Source:** `tests/test_symbol_burst_live.cpp`, paced observations/selectors;
  real production Live queue/frontier/age counters. See the continuation's
  remaining-coverage section and existing `backlog_rescue_coverage_gap` findings.
- **In-progress work:** the Sol-owned private candidate is under
  `.agent-work/artifacts/symbol-burst-postupdate-20261018/default-media-clock-implementation/paced-rescue-fixture-root-sol-helper/`.
  Its saved static result reports `needs_decision` and a **failed RAII release
  ordering check**. It is not accepted implementation or runtime evidence.
  Reconcile its later review before using it; do not restart it under a new ID.
- **Acceptance:** independently paced real PCM; genuine queued work/oldest age;
  exclude startup debt using the recorded fence; preserve existing material-debt
  thresholds; show qualifying drains during legal gaps and matched controls.
  A held consumer must be released safely on every failure/cancellation path.
  Do not block the producer to manufacture rescue or fabricate counters.
- **Ownership:** Sol owns the unresolved stimulus/lifetime design and private
  fixture. Luna may independently read existing gap evidence or implement a
  genuinely separable settled successor after review. This does not require a
  production performance patch unless evidence identifies one.

### 4. Local-fit geometry, sparse support and phase controls — runtime unverified

- **Possible errors:** full coherent signals lose support; aligned/shifted sparse
  signals still gain full support; missing observations gain credit; quarter-phase
  changes are penalized; tiny/rank-deficient geometry divides incorrectly.
- **Source/check:** `src/pattern_correlator_batch.hpp`,
  `correlation_temporal_support_sections` and temporal-fit helpers;
  `tests/test_pattern_correlator_batch.cpp::temporal_support_tracks_observed_distribution()`.
  Existing static work already checked 55,320 geometry cases; do not repeat it
  as a substitute for the C++ checks.
- **Acceptance:** adaptive 8/4/2 partitions obey minimum actual observation counts;
  tiny geometry follows the stated fallback; full and phase controls preserve
  support; half/shifted-half, missing-sample and singular cases remain bounded.
- **Parallel lane:** Luna evidence extraction/math readback from the single
  component result; any repair to this existing Sol assignment remains Sol-owned.

### 5. FFT acquisition/continuation and genuine Burst test routing — runtime unverified

- **Possible errors:** the new ceiling misses acquisition or weak-confirmed
  continuations, recomputes from expired input, or a regression accidentally tests
  ordinary mode rather than the failing Burst path.
- **Source/check:** `src/pattern_receiver.cpp` and
  `test_pattern_receiver --burst-temporal-support`; the paced keyed On/follow
  cases exercise automatic profile competition end to end.
- **Acceptance:** actual keyed Burst slot PCM, not a contiguous modulo waveform;
  selected bit/phase and physical absence retained; finite native operands and
  local counts; no arbitrary coordinate-to-family join. Keep full, sparse,
  shifted and phase-control assertions.
- **Known trap:** an earlier Luna regression never enabled Burst and was rejected.
  Its corrected implementation is an existing Sol takeover, not a fresh Luna task.
- **Parallel lane:** independent Luna source/trace coverage readback; no overlapping
  `src/pattern_receiver.cpp` or `tests/test_pattern_receiver.cpp` edits.

### 6. Correlator fallback raw/affine/pulse paths — runtime unverified

- **Possible errors:** source partitions overlap pulse cells, fractional rate/origin
  rounds incorrectly, chunk boundaries change support, or affine coefficient reuse
  credits unobserved portions.
- **Source/check:** `src/pattern_correlator.cpp`,
  `tests/test_pattern_correlator.cpp::symbol_burst_temporal_support()`;
  focused command `test_pattern_correlator symbol_burst_temporal_support`.
- **Acceptance:** raw, aligned pulse, unaligned wide-chip clipped affine and
  chunked paths preserve original bit decisions and physical completion while
  sparse support is bounded. Preserve legitimate drift-quarter phase changes.
- **Parallel lane:** Luna reads this distinct component artifact while another
  reads FFT results; production fallback repair stays with its original Sol owner.

### 7. Mandatory workspace, detector funding and planner accounting — runtime unverified

- **Possible errors:** new mandatory state silently displaces an old drift or
  differential detector; allocation exceeds the budget; release/resize leaks
  retained state; planner estimates disagree with actual allocation.
- **Source:** `src/pattern_correlator.cpp`, `src/simulation_estimate.cpp`,
  `tests/test_live_resources.cpp`, `tests/test_receiver_strategy.cpp`,
  `tests/test_simulation_estimate.cpp`, existing tight-workspace Correlator tests.
- **Known review failure:** the first private draft displaced detector funding;
  it was rejected and reworked before integration. Do not reapply that draft.
- **Acceptance:** original detector choices remain intact; exact mandatory storage
  is accounted; insufficient aggregate budget fails explicitly; no silent lost
  acquisition coverage. Compare actual working/reserved/peak bytes and projections.
  Preserve the existing numeric saturation guards and boundary tests.
- **Parallel lane:** Luna reconciles existing allocation/estimate evidence and
  identifies a precise missing assertion; Sol decides any resource-contract fix.
  Reuse the accepted GNU/MSVC numeric checks where their inputs remain unchanged.

### 8. Amplitude, differential, weak/drift and physical-end controls — latest correction unqualified

- **Risk:** a supposedly isolated support change affects alternate modes, weak
  reception, phase/drift handling, pending bits or six fully scored seconds of
  absence. These are regression risks, not newly observed failures.
- **Source/checks:** `tests/test_receiver_differential.cpp`,
  `tests/test_weak_signal.cpp`, `tests/test_pattern_receiver.cpp`,
  `tests/test_symbol_burst_receiver.cpp`, original wire/physical-end cases.
- **Acceptance:** all applicable original cases and calibration gates complete;
  amplitude/differential behavior stays unchanged; EOF, cancellation, codec end
  and a correct payload never substitute for observed physical absence.
- **Parallel lane:** Luna inventory/result reconciliation using the required full
  jobs; reuse their results rather than launching redundant standalone heavy runs.

### 9. Input integrity, pacing validity and trace completeness — current runtime unmeasured

- **Possible errors:** compare changed waveforms, confuse generated/prefetched
  samples with transmitted input, truncate input after an assertion, overrun a
  queue, or infer no events from an incomplete/incorrect-backend trace.
- **Source/check:** paced wrapper in `.github/workflows/native-linux.yml`,
  `tests/test_symbol_burst_live.cpp`, actual source/PCM/progress/closure records.
- **Acceptance:** record exact source/configuration/key/input identity, generated
  versus transmitted versus captured/processed counts, queue and health, pace
  lateness, bounded event sequences/footer/external closure/drop counts. Keep
  individual deadlines. One timing pair cannot establish instrumentation overhead
  or a reliable speedup; match actual PCM for diagnostic pairs.
- **Parallel lane:** Luna reads the capture/manifest independently of fit analysis.
  Existing failed reader assignments remain Sol-owned; do not disguise their repair
  as a new scout or recreate previously corrected parsers.

### 10. Strict GUI stop, source tail, pending progress and missing Help — latest-source reruns pending

- **Possible regressions:** stop exceeds ten seconds; source passes the protected
  tail; a verified result bypasses pending progress; a completed raw `Help` result
  is overwritten or cannot be copied exactly.
- **Source/checks:** `tests/test_gui_controller.cpp`, `src/gui/gui_smoke.cpp`,
  `src/live_replay_staging.hpp`, original GUI serial/parallel and copied TGZ/ZIP
  smokes on Ubuntu/Debian. Preserve all original assertions.
- **Acceptance:** both original GUI tests in each serial/parallel mode pass;
  actual EU stop <=10 seconds with tail/arrival checks; independent terminal IDs
  and matching results survive staging; exact `Help` bytes/text arrive. Ordinary
  five-second advisory fields do not replace the hard instrumented ten-second check.
- **History:** these checks passed on earlier candidates, including eight copied
  backend/distribution/archive smokes. Original missing-Help attribution remains
  uncertain because the original failing key/PCM were lost. Do not repeat a random
  key grid just to explain that historical incident; retain future failed inputs.
- **Parallel lane:** Luna GUI/source-tail readback and a separate package-evidence
  readback can proceed from the same integrated revision without shared writes.

### 11. Full matrix, test inventory and binary identity — delivery gates pending

- **Gap:** the current runtime correction has not passed full native/SDK coverage.
  Old green workflows do not qualify newly changed receiver binaries.
- **Route:** after accepted focused and original strict GUI results, launch the
  required `ci.yml` and `sdk.yml` concurrently on one immutable accepted source,
  standard runners, `devfast=false`; reuse the established inputs/base SDK recipes.
  The supervisor performs shared dispatch/Git operations and reconciles existing
  jobs first. Functional failures, sanitizer findings and missing assertions stay
  fatal; never count skipped/cancelled/queued cases as passes.
- **Parallel lane:** distinct Luna result readers for native Release/Debug/Windows,
  calibration aggregation, SDK contracts, and copied packages as their outputs
  become ready. Do not spawn each reader before an independently useful result
  exists. A settled manifest/count helper or evidence draft may be Luna-owned in
  a private output path, with independent review before authoritative acceptance.
- **Acceptance:** reconcile JUnit with exact selected registrations; every required
  fixed calibration seed and aggregate gate; source/configuration/archive/binary
  hashes; mandatory platform/SDK/packaging coverage and exact omissions/warnings.
  Do not claim an outer artifact digest is an inner archive or executable digest.

## Cheap, useful parallelism and dependency order

Use the installed cost-saving roles for suitable work:

- `luna_worker`: **gpt-6-luna / high**, for substantial bounded implementation
  with settled interfaces/acceptance and disjoint ownership. Let it choose ordinary
  implementation details and run authorized proportionate checks.
- `luna_scout`: **gpt-6-luna / high**, for bounded read-only source/inventory/
  artifact questions. Existing evidence should be reused, not rediscovered.
- Original strong owner / `sol_expert`: consequential diagnosis, shared receiver
  invariants, resource/lifetime decisions, Sol takeovers and independent acceptance.
  Installed `sol_expert` is **gpt-6.1-sol / ultra**. Do not change the selected
  primary model, role configuration or inference tier to implement this document.

These are installed routing choices, not a claim about the globally cheapest
model or permission to hand existing Sol takeovers back to cheaper workers.
The main supervisor dispatches; helpers do not spawn helpers or self-approve.
On a Luna local failure, stop with original evidence; only the supervisor can
allow the single evidence-based repair where still available. Preserve consumed
allowances. Setup recovery is not a new assignment or a permission bypass.

Useful concurrent lanes are **component fit evidence (4–6)**,
**workspace/alternate-mode coverage (7–8)**, **input/trace integrity (9)**,
**GUI/package results (10)**, and **matrix inventory/readback (11)**, while the
original Sol owner completes the compile repair and backlog-fixture design
(1–3). These are possible partitions, not a quota or an instruction to spawn
one agent per item. Reuse currently active helpers and completed handoffs.
Every packet needs one question/outcome, immutable input, exact allowed paths,
known trap if applicable, existing acceptance route and compact evidence output.
Independent readbacks of one decisive capture can run together; overlapping
production edits cannot.

Dependency order:

```text
reconcile actual current work / preserve ownership and dirty changes
  -> original-owner compile repair + independent stable support work
  -> one integrated focused run; read its component/capture/GUI results in parallel
  -> close wrong-final behavior and actual backlog-rescue coverage
  -> full native and SDK concurrently on the accepted source
  -> independent result/inventory/hash acceptance
  -> release/publication/certification only if separately requested/authorized
```

Do not turn broad agent availability into duplicate CI or local memory pressure.
At most **five independent useful standard hosted diagnostic runners** may run
concurrently; that is a ceiling. Full qualification has its existing separate
scope. Local guards remain in the current handoff: one compiler, fresh available
memory >8 GiB before compilation, existing process/time/memory limits, and no
compiler/application overlap. Read actual free memory before choosing local work.
No build, CI dispatch, model change or release was launched by writing this file.

## Count checkpoint: executions, not errors

The earlier **19:59 UTC** planning subtotal was **944 executions**:

| Coverage | Executions |
|---|---:|
| Focused route before any added held-rescue cases | 15 |
| Linux Release core, previous successful cases | 252 |
| Linux sanitizer Debug core, previous successful cases | 249 |
| Windows Release core, previous successful cases | 231 |
| SDK general + Live + calibration (194 + 2 + 1) | 197 |
| **Historical planning subtotal** | **944** |

The 15 comprised original no-argument Live (1), paced Off/On plain/keyed (4),
full-input follow (1), three component selectors (3), FFT/search (2), GUI serial
(2) and GUI parallel (2). They did not execute in failed run `39487011213`.
Added rescue cases or an updated route change that subtotal; reconcile the actual
new inventory. Additional frontend/web/package/copied-binary checks were not
included. Platform repeats and focused/full reruns are separate executions;
these are not 944 unique definitions, never-tested features or known defects.
Declared runtime omissions were excluded from the native counts above.

For the exact individual test names use the retained selected inventories and
JUnit reports, not job counts or this subtotal. Start with the accepted
[Debug core inventory](.agent-work/artifacts/symbol-burst-postupdate-20261018/default-media-clock-implementation/production-qualification-a6c2bf1/native-final-debug-core-1e9b-readback/extracted/__w/pumpModem/pumpModem/build/ci-test-report.inventory.json),
[Release core readback](.agent-work/artifacts/symbol-burst-postupdate-20261018/default-media-clock-implementation/production-qualification-a6c2bf1/native-final-ready-core-1e9b-readback/readback.json),
and [SDK contract readback](.agent-work/artifacts/symbol-burst-postupdate-20261018/default-media-clock-implementation/production-qualification-a6c2bf1/sdk-final-fltk-contract-1e9b-readback/readback.json).
An inventory lists registrations, not proof that every registration ran.

## Reuse accepted results and preserve hard gates

Earlier source `1e9b83238a50bd7983c5b2301f22ac451b42a7b0` completed
[native 39462367772](https://github.com/mirage335-colossus/pumpModem/actions/runs/39462367772)
and [SDK 39462388103](https://github.com/mirage335-colossus/pumpModem/actions/runs/39462388103)
successfully with documented warnings/omissions. Both matrices and the corrected
copied Rev smokes are retained evidence, not qualification of `049e84e` or its
successors. The earlier GUI stop, workspace and missing-Help observations are
separate until evidence connects them. Do not reopen resolved profile/exit-77
choices, completed TUI investigations or closed assignments.

Preserve ten-second GUI stop, six fully scored seconds of physical absence,
source tail, next-poll prefix/pending identity, all key/profile coverage and input
integrity. Only the explicitly approved compact hosted Live 40/120 throughput
limit may produce its narrow incomplete-coverage warning with complete functional
analogue and remaining assertions. Missing Help/pending data, wrong bits, stalls,
crashes and sanitizer findings remain failures; do not reclassify them as timing
warnings. Retain applicable existing typed cumulative-smoke policy separately.

This checklist creates no release/publication authorization. A focused green
run is not full qualification; a compiled archive is not a qualified public
release. If release delivery is requested, follow the existing exact-source and
binary certification/publication rules after required acceptance.
