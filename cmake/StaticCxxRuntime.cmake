include_guard(GLOBAL)

set(DATAPUMP_STATIC_GNU_RUNTIME OFF)
if(DATAPUMP_PORTABLE AND NOT DATAPUMP_SANITIZERS AND
    (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
      (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")))
  function(datapump_check_gnu_cxx_runtime output)
    include(CheckCXXSourceCompiles)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_REQUIRED_QUIET TRUE)
    # Inspect the selected headers, rather than assuming Clang uses libstdc++.
    # Recheck on reconfiguration so a changed -stdlib/toolchain cannot reuse a
    # result from a different C++ runtime.
    unset(datapump_has_gnu_cxx_runtime CACHE)
    check_cxx_source_compiles("#include <cstddef>
      #ifndef __GLIBCXX__
      #error The selected standard library is not libstdc++
      #endif
      int main() { return 0; }" datapump_has_gnu_cxx_runtime)
    set(${output} "${datapump_has_gnu_cxx_runtime}" PARENT_SCOPE)
    unset(datapump_has_gnu_cxx_runtime CACHE)
  endfunction()
  datapump_check_gnu_cxx_runtime(DATAPUMP_STATIC_GNU_RUNTIME)
endif()

function(datapump_link_static_cxx_runtime target visibility)
  if(DATAPUMP_STATIC_GNU_RUNTIME)
    # A bundled old libstdc++.so would also be selected by newer host graphics
    # drivers through the application's inherited RPATH. Keep our runtime
    # private, so those drivers can load and bind to their own C++ runtime.
    target_link_options(${target} ${visibility} -static-libstdc++ -static-libgcc)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_options(${target} ${visibility}
        "LINKER:--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a")
    endif()
  endif()
endfunction()
