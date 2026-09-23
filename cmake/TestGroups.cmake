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
set(_script_tests legacy_boundary fast_boundary vendored_lzma_source
  gui_adapter_boundary gui_boundary_regression packaging_support sdk_packaging build_wrapper build_dependencies build_source_sdk build_sdk_runtime)
set(_aliases
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
  "windows_audio_contract=test_windows_audio"
  "fast_acoustic_short_convolutional=test_fast_acoustic_short_weak"
  "gui_self_check=datapump-gui"
  "gui_workflow=datapump-gui"
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
  live_profiles live_receptions live live_resources live_transmit_lock compression_short transfer
  stream_codec stream_receive recovery attachment pattern_correlator pattern_receiver
  pattern_drift pattern_differential receiver_differential pattern_fft_batch
  pattern_correlator_batch pattern_search tuning simulation_estimate
  receiver_probability differential_probability differential_receiver_probability
  weak_signal gui_application gui_controller gui_inspection gui_binary_editor cli)

foreach(_test IN LISTS _tests)
  get_property(_labels TEST ${_test} PROPERTY LABELS)
  if(_test MATCHES "^fast_" OR _test MATCHES "^gui_fast")
    list(APPEND _labels fast)
  elseif(_test MATCHES "^legacy_" OR _test MATCHES "^gui_legacy")
    list(APPEND _labels legacy)
  elseif(_test MATCHES "^build_" OR _test STREQUAL "vendored_lzma_source")
    list(APPEND _labels build)
  elseif(_test STREQUAL "packaging_support" OR _test STREQUAL "native_relocation" OR _test STREQUAL "sdk_packaging")
    list(APPEND _labels packaging)
  elseif(NOT "gui" IN_LIST _labels AND NOT _test STREQUAL "rev_utf8")
    list(APPEND _labels regular)
  endif()
  if(_test STREQUAL "rev_utf8")
    list(APPEND _labels gui)
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
  foreach(_group IN LISTS _labels)
    list(APPEND _group_${_group} ${_dependencies})
  endforeach()
  list(APPEND _all_test_targets ${_dependencies})
endforeach()

add_custom_target(datapump-tests DEPENDS ${_all_test_targets})
foreach(_group contract regular fast legacy gui native packaging build)
  if(_group STREQUAL "native")
    set(_label native_gui)
  else()
    set(_label ${_group})
  endif()
  add_custom_target(datapump-tests-${_group} DEPENDS ${_group_${_label}})
endforeach()
