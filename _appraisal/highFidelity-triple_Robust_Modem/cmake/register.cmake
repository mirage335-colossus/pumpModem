# Configure the canonical root so its source-relative dependency contracts hold.
# Defer registration until all of the canonical native libraries exist.
function(datapump_register_high_fidelity_appraisal)
  # Baseline CPU-only probe calls the generic GPU factory but conditionally
  # includes its declaration. Supply that declaration only for this test probe
  # in the contained appraisal build; do not pretend an accelerator is enabled.
  if(TARGET triad_receiver_probe AND NOT DATAPUMP_TRIAD_TENSOR AND NOT DATAPUMP_TRIAD_VULKAN)
    if(MSVC)
      target_compile_options(triad_receiver_probe PRIVATE "/FI${PROJECT_SOURCE_DIR}/include/datapump/triad_acceleration.hpp")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(triad_receiver_probe PRIVATE -include "${PROJECT_SOURCE_DIR}/include/datapump/triad_acceleration.hpp")
    endif()
  endif()
  if(NOT TARGET high_fidelity_appraisal)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../CMakeLists.txt")
  endif()
endfunction()
cmake_language(DEFER CALL datapump_register_high_fidelity_appraisal)
