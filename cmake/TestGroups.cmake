# Keep existing individual CTest cases and their isolation. These aggregate
# targets compile exactly the prerequisites of a selected functional group.
get_property(_tests DIRECTORY PROPERTY TESTS)
get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _targets)
  if(_target MATCHES "^test_" OR _target STREQUAL "fast_regression" OR _target STREQUAL "alsa_stub")
    set_property(TARGET ${_target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()

# Exceptions to the usual test_<CTest name> convention. Scripts with no
# executable prerequisite are listed explicitly so new cases cannot silently
# run without their required build step.
set(_script_tests legacy_boundary fast_boundary vendored_lzma_source packaging_cxx_runtime build_capacity
  build_agent_record build_agent_board build_agent_session build_agent_edit build_agent_stress
  build_release_web build_wasm_sdk_release build_native_sdl web_renderer browser_audio web_preview_client build_preview build_wasm_sdk build_package_wasm
  gui_adapter_boundary gui_boundary_regression packaging_support sdk_packaging build_wrapper build_gui_smoke_runner build_native_tests_runner build_ci_tests build_ci_test_plan build_ci_preparation_plan build_ci_build_probe build_dependencies build_source_sdk build_sdk_runtime build_release build_release_dependencies build_sdk_release build_certify_release build_verify_latest_release build_windows_certification build_release_rev_tools build_windows_base build_ci_apt build_apt_release build_distro_release build_arch_release build_gentoo_sync)
set(_aliases
  "amplitude_arrival_strategy=test_receiver_strategy"
  "amplitude_live_arrival=test_live_profiles"
  "amplitude_arrival_planning=test_live_planning_link"
  "amplitude_arrival_detector=test_pattern_envelope"
  "amplitude_arrival_arbitration=test_live_receptions"
  "amplitude_older_probability_compat=test_simulation_estimate"
  "amplitude_axis_reference_compat=test_frequency_axis_probability"
  "amplitude_planner_probability=test_gui_link_planner"
  "live_exact_source=test_live_profiles"
  "scheduled_fine_curves_65=test_scheduled_fine_curves"
  "scheduled_fine_curves_disabled_policy=test_scheduled_fine_curves"
  "temporal_fine_curve_borrow=test_temporal_templates"
  "temporal_fine_curve_cached=test_temporal_templates"
  "scheduled_fine_curves_cached=test_scheduled_fine_curves"
  "web_worker=datapump-worker"
  "web_live=datapump-worker"
  "web_preview=datapump-worker"
  "wasm_runtime=datapump-wasm-page"
  "wasm_live=datapump-wasm-page"
  "gui_document_layout=test_document_layout"
  "gui_record_reconciliation=test_record_reconciliation"
  "gui_document_presentation=test_document_presentation"
  "gui_inspection=test_inspection"
  "gui_pattern_space=test_pattern_space"
  "gui_bitmaps=test_gui_bitmaps"
  "gui_inspection_page=test_inspection_page"
  "gui_binary_editor=test_binary_editor"
  "gui_profile_reference=test_profile_reference"
  "gui_native_policy=test_gui_native"
  "alsa_audio_contract=test_alsa_audio"
  "alsa_interleave_playback=test_alsa_audio"
  "windows_audio_contract=test_windows_audio"
  "fast_acoustic_short_convolutional=test_fast_acoustic_short_weak"
  "gui_self_check=datapump-gui"
  "gui_workflow=datapump-gui"
  "tui_self_check=datapump-tui"
  "tui_terminal=datapump_tui_adapter_test"
  "tui_winconsole=test_winconsole"
  "fb_self_check=datapump-fb"
  "fb_headless=datapump-fb"
  "fb_sdl=datapump-fb"
  "gui_adapter_conformance=test_${DATAPUMP_GUI_BACKEND}_adapter"
  "gui_document_conformance=test_fltk_document"
  "gui_platform_conformance=test_rev_platform"
  "gui_coordinates_1x=test_rev_coordinates"
  "gui_coordinates_2x=test_rev_coordinates"
  "cli=pump" "fast_cli=pump" "fast_cable_benchmark=pump"
  "build_speculation_codegen=test_speculation"
  "boundary_marker_storage=pump" "fast_snr=fast_regression")
foreach(_alias IN LISTS _aliases)
  string(REPLACE "=" ";" _pair "${_alias}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _dependency)
  set(_prerequisites_${_name} ${_dependency})
endforeach()
set(_prerequisites_native_relocation ${datapump_executables})

# Keep this list aligned with docs/development.md. Calibration stays included:
# a cheap smoke selection must never replace the preservation contract.
set(_contract
  symbol_burst symbol_burst_transmit symbol_burst_receiver symbol_burst_live
  scheduled_native_coherent_prefit scheduled_native_cohort
  scheduled_original_pulse_row scheduled_exact_source_points
  scheduled_cold_colored_source_rows
  live_planning_link live_snapshot_copy live_monitor_frame
  scheduled_workspace_accounting
  scheduled_float_spectral_accumulator scheduled_float_acquisition_tile scheduled_cold_colored_runtime scheduled_cold_colored_proxy_runtime
  live_workspace_capacity scheduled_acquisition_memory scheduled_capture_layout scheduled_carrier_restart scheduled_cold_colored_certificate scheduled_exact_source_samples scheduled_frequency_postinverse scheduled_lazy_acquisition scheduled_memory_projection scheduled_original_projection_cache scheduled_processing_journal scheduled_acquisition_tile scheduled_physical_multidwell scheduled_polynomial_chip_moments scheduled_polynomial_chip_runtime scheduled_polynomial_chip_fft scheduled_integral_colored scheduled_origin_gram scheduled_origin_gram_family scheduled_origin_gram_bulk scheduled_zero_guard scheduled_linear_native_bound scheduled_linear_gain_cells scheduled_linear_direct_runs scheduled_linear_fragments scheduled_default_linear_cellstats scheduled_default_linear_centers scheduled_default_linear_events scheduled_default_linear_runtime scheduled_default_linear_fhss scheduled_default_linear_fhss_original scheduled_default_linear_original scheduled_default_point_fft scheduled_default_point_observation scheduled_default_point_fhss scheduled_default_point_guard_archive scheduled_default_point_guard_edges scheduled_default_prepared_endstrips scheduled_default_point_guard_edges_runtime scheduled_default_point_guard_archive_runtime scheduled_default_prepared_events scheduled_default_point_fft_runtime scheduled_default_point_family scheduled_default_point_template_cache scheduled_default_point_template_family scheduled_default_point_bulk scheduled_default_point_bulk_original scheduled_default_point_guard_summary scheduled_default_compact_chords scheduled_default_point_compact scheduled_default_point_reference_recipe scheduled_default_point_compact_runtime scheduled_point_guard_prepass scheduled_point_guard_prepass_runtime scheduled_default_point_diagnostics scheduled_default_guard_cohort scheduled_point_source_nodes scheduled_linear_family scheduled_linear_grid_original scheduled_template_cpu_attribution scheduled_fractional_basis scheduled_pulse_chord_certificate scheduled_gain_chord scheduled_pulse_derivative_chord_certificate scheduled_gain_derivative_hook scheduled_fractional_basis_runtime scheduled_fractional_basis_original scheduled_origin_gram_original scheduled_cold_owned_proxy scheduled_general_colored scheduled_colored_lattice scheduled_clock_reference scheduled_partial_colored scheduled_partial_colored_runtime scheduled_residual_prefix scheduled_acquisition_catalog scheduled_acquisition_quarters shared_scheduled_capture
  scheduled_certificate_diagnostics scheduled_cancellation_resume scheduled_template_proxy_certificate scheduled_diagnostic_journal scheduled_parallel_preparation
  scheduled_frequency_piece scheduled_private_batch scheduled_detached_replay scheduled_detached_resume
  scheduled_fine_curves_disabled_policy temporal_fine_curve_borrow temporal_fine_curve_cached scheduled_fine_curves_cached
  scheduled_fine_curves scheduled_fine_curves_65 component_avx2 component_avx2_portable outer_public_replay temporal_entry_payload fp32_packed_l1 scheduled_r1_export scheduled_flat_export temporal_outer_plan temporal_fp32_adapter cached_pulse_count pattern_coarse_cache shared_temporal_plans certified_ordered_plan temporal_plan_cache borrowed_plan_prepare temporal_pool_consumer temporal_normalization borrowed_ordered_plan temporal_templates fine_cache_preservation rx_cpu_profile timing_continuation pattern_startup_guide physical_moment_cache pattern_shared_source_curve pattern_permutation_integration scheduled_pulse_moments scheduled_float_signal_moments scheduled_float_gram gram_override scheduled_mapped_piece_groups sched_hop scheduled_symbol_calendar sched_hop_transmit sched_hop_receiver sched_hop_complex sched_hop_correlator scheduled_complex_search scheduled_frequency_fusion sched_capture
  live_exact_source live_profiles live_receptions live live_resources live_transmit_lock compression_short transfer
  stream_codec stream_receive recovery attachment pattern_correlator pattern_receiver clock_sync
  pattern_drift pattern_differential receiver_differential pattern_fft_batch search_fft search_arithmetic
  pattern_correlator_batch pattern_search pattern_start_geometry tuning simulation_estimate
  receiver_probability frequency_axis_probability amplitude_probability amplitude_arrival_strategy amplitude_live_arrival differential_probability differential_receiver_probability
  weak_signal gui_application gui_controller gui_timing_advice gui_scheduled_cold_start gui_inspection gui_binary_editor cli)
list(APPEND _contract scheduled_owned_fft_window_filter scheduled_owned_fft_window_hook scheduled_owned_moment_runtime scheduled_owned_moments scheduled_sparse_owned_spans scheduled_owned_moment_correction scheduled_owned_field_errors scheduled_sparse_carrier)
set(_ci_live_tests live live_profiles)

# Export the resolved registration/prerequisite relation for the CI planner.
# This is generated from the same loop that validates every CTest entry below,
# so the planner does not maintain a second test-to-target map.
set(_ci_prerequisites_json "{\n  \"schema\": 1,\n  \"tests\": [")
set(_ci_prerequisites_first TRUE)

foreach(_test IN LISTS _tests)
  get_property(_labels TEST ${_test} PROPERTY LABELS)
  if(_test MATCHES "^fast_" OR _test MATCHES "^gui_fast")
    list(APPEND _labels fast)
  elseif(_test MATCHES "^legacy_" OR _test MATCHES "^gui_legacy")
    list(APPEND _labels legacy)
  elseif(_test MATCHES "^build_" OR _test STREQUAL "vendored_lzma_source")
    list(APPEND _labels build)
  elseif(_test MATCHES "^packaging_" OR _test STREQUAL "native_relocation" OR _test STREQUAL "sdk_packaging")
    list(APPEND _labels packaging)
  elseif(NOT "gui" IN_LIST _labels AND NOT "frontends" IN_LIST _labels AND NOT "web" IN_LIST _labels AND NOT _test STREQUAL "rev_utf8")
    list(APPEND _labels regular)
  endif()
  if(_test STREQUAL "rev_utf8")
    list(APPEND _labels gui)
  endif()
  if(_test STREQUAL "scheduled_parallel_preparation")
    list(APPEND _labels preparation)
  endif()
  if(_test IN_LIST _contract)
    list(APPEND _labels contract)
  endif()
  list(REMOVE_DUPLICATES _labels)
  set_property(TEST ${_test} PROPERTY LABELS "${_labels}")

  if(DEFINED _prerequisites_${_test})
    set(_dependencies ${_prerequisites_${_test}})
  elseif(TARGET test_${_test})
    set(_dependencies test_${_test})
  elseif(_test MATCHES "^fast_acoustic_short_snr_")
    set(_dependencies test_fast_acoustic_short)
  elseif(_test IN_LIST _script_tests)
    set(_dependencies "")
  else()
    message(FATAL_ERROR "Declare the build prerequisites for CTest case '${_test}' in cmake/TestGroups.cmake")
  endif()
  foreach(_dependency IN LISTS _dependencies)
    if(NOT TARGET ${_dependency})
      message(FATAL_ERROR "Unknown test prerequisite: ${_test} -> ${_dependency}")
    endif()
  endforeach()
  if(NOT _ci_prerequisites_first)
    string(APPEND _ci_prerequisites_json ",")
  endif()
  set(_ci_prerequisites_first FALSE)
  string(APPEND _ci_prerequisites_json "\n    {\"name\": \"${_test}\", \"targets\": [")
  set(_ci_prerequisite_first TRUE)
  foreach(_dependency IN LISTS _dependencies)
    if(NOT _ci_prerequisite_first)
      string(APPEND _ci_prerequisites_json ", ")
    endif()
    set(_ci_prerequisite_first FALSE)
    string(APPEND _ci_prerequisites_json "\"${_dependency}\"")
  endforeach()
  string(APPEND _ci_prerequisites_json "]}")
  foreach(_group IN LISTS _labels)
    list(APPEND _group_${_group} ${_dependencies})
  endforeach()
  if(_test IN_LIST _ci_live_tests)
    list(APPEND _ci_live_targets ${_dependencies})
  elseif(_test STREQUAL "scheduled_parallel_preparation")
    list(APPEND _ci_preparation_targets ${_dependencies})
  elseif(NOT "fast" IN_LIST _labels AND NOT "calibration" IN_LIST _labels AND NOT "frontends" IN_LIST _labels AND NOT "web" IN_LIST _labels)
    list(APPEND _ci_core_targets ${_dependencies})
  endif()
  list(APPEND _all_test_targets ${_dependencies})
endforeach()

string(APPEND _ci_prerequisites_json "\n  ]\n}\n")
file(WRITE "${CMAKE_BINARY_DIR}/ci-test-prerequisites.json" "${_ci_prerequisites_json}")

add_custom_target(datapump-tests DEPENDS ${_all_test_targets})
add_custom_target(datapump-tests-ci-core DEPENDS ${_ci_core_targets})
add_custom_target(datapump-tests-ci-live DEPENDS ${_ci_live_targets})
add_custom_target(datapump-tests-ci-preparation DEPENDS ${_ci_preparation_targets})
foreach(_group contract regular fast legacy gui frontends web native packaging build amplitude_probability amplitude_arrival)
  if(_group STREQUAL "native")
    set(_label native_gui)
  else()
    set(_label ${_group})
  endif()
  add_custom_target(datapump-tests-${_group} DEPENDS ${_group_${_label}})
endforeach()
