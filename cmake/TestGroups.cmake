# Build exactly the dependencies of each surviving test group.
get_property(_tests DIRECTORY PROPERTY TESTS)
get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _targets)
  if(_target MATCHES "^test_" OR _target STREQUAL "fast_regression" OR _target STREQUAL "alsa_stub")
    set_property(TARGET ${_target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
set(_aliases
  "gui_document_layout=test_document_layout"
  "gui_record_reconciliation=test_record_reconciliation"
  "gui_document_presentation=test_document_presentation"
  "gui_inspection_page=test_inspection_page"
  "gui_binary_editor=test_binary_editor"
  "gui_native_policy=test_gui_native"
  "gui_robust_gpu=test_gui_robust_live"
  "gui_robust_gpu_fp32=test_gui_robust_live"
  "gui_robust_gpu_native_lag=test_gui_robust_live"
  "gui_robust_gpu_unavailable=test_gui_robust_live"
  "alsa_audio_contract=test_alsa_audio"
  "alsa_interleave_playback=test_alsa_audio"
  "alsa_continuous_clock=test_alsa_audio"
  "windows_audio_contract=test_windows_audio"
  "gui_fast_keygen=test_gui_fast_live"
  "fast_acoustic_short_convolutional=test_fast_acoustic_short_weak"
  "gui_self_check=datapump-gui" "gui_workflow=datapump-gui"
  "tui_self_check=datapump-tui" "tui_terminal=datapump_tui_adapter_test"
  "tui_paste_boundaries=datapump_tui_adapter_test" "tui_winconsole=test_winconsole"
  "fb_self_check=datapump-fb" "fb_headless=datapump-fb" "fb_sdl=datapump-fb"
  "gui_adapter_conformance=test_${DATAPUMP_GUI_BACKEND}_adapter"
  "gui_document_conformance=test_fltk_document" "gui_platform_conformance=test_rev_platform"
  "gui_coordinates_1x=test_rev_coordinates" "gui_coordinates_2x=test_rev_coordinates"
  "fast_cli=pump" "fast_cable_benchmark=pump" "fast_snr=fast_regression"
  "triad_vulkan_plan=test_triad_vulkan"
  "triad_vulkan_host_scoring=test_triad_vulkan"
  "triad_vulkan_workspace_admission=test_triad_vulkan"
  "triad_tensor_cuda_half_fft=test_triad_tensor_fft"
  "build_speculation_codegen=test_speculation"
  "web_worker=datapump-worker" "web_preview=datapump-worker"
  "wasm_runtime=datapump-wasm-page" "wasm_live=datapump-wasm-page" "web_live=datapump-worker"
  "audio_entropy_helper_filter=test_audio_entropy_helper_filter")
foreach(_alias IN LISTS _aliases)
  string(REPLACE "=" ";" _pair "${_alias}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _dependency)
  set(_prerequisites_${_name} ${_dependency})
endforeach()
set(_prerequisites_native_relocation ${datapump_executables})
# These suites validate only orchestration/statistics. Native subprocess suites
# declare test_<name> targets so the production receiver is always built first.
foreach(_pure_triad triad_abbreviated triad_cases triad_gpu_diagnostics)
  set(_prerequisites_${_pure_triad} "")
endforeach()
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
  elseif(_test MATCHES "^triad_")
    list(APPEND _labels triad)
  elseif(_test MATCHES "^gui_" OR _test MATCHES "^rev_")
    list(APPEND _labels gui)
  elseif(NOT "frontends" IN_LIST _labels AND NOT "web" IN_LIST _labels)
    list(APPEND _labels regular)
  endif()
  if(_test MATCHES "^(audio_entropy|keyfile_entropy|speculation)$")
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
  elseif(_test MATCHES "^build_" OR
      _test MATCHES "^(legacy_boundary|fast_boundary|vendored_lzma_source|gui_adapter_boundary|gui_boundary_regression|packaging_support|packaging_cxx_runtime|sdk_packaging|web_renderer|browser_audio|web_preview_client)$")
    set(_dependencies "")
  else()
    message(FATAL_ERROR "Declare build prerequisites for '${_test}' in cmake/TestGroups.cmake")
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
add_custom_target(datapump-tests-ci-core DEPENDS ${_group_contract} ${_group_regular})
foreach(_group contract regular fast legacy gui frontends web native packaging build triad)
  if(_group STREQUAL "native")
    set(_label native_gui)
  else()
    set(_label ${_group})
  endif()
  add_custom_target(datapump-tests-${_group} DEPENDS ${_group_${_label}})
endforeach()
