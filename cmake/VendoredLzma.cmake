# Pinned, offline source build. No find_package(liblzma), download, DLL or
# dependency on the destination computer's compression libraries is involved.
function(datapump_add_lzma)
  set(BUILD_SHARED_LIBS OFF)
  foreach(option XZ_NLS XZ_DOC XZ_DOXYGEN XZ_TOOL_XZ XZ_TOOL_XZDEC
      XZ_TOOL_LZMADEC XZ_TOOL_LZMAINFO XZ_TOOL_SCRIPTS XZ_MICROLZMA_ENCODER
      XZ_MICROLZMA_DECODER XZ_LZIP_DECODER XZ_EXTERNAL_SHA256)
    set(${option} OFF CACHE BOOL "Disabled for the private DataPump codec" FORCE)
  endforeach()
  set(XZ_THREADS no CACHE STRING "Single-threaded bounded codecs" FORCE)
  set(XZ_SANDBOX no CACHE STRING "No XZ command-line tools" FORCE)
  set(XZ_ENCODERS "lzma1;lzma2" CACHE STRING "Private codec filters" FORCE)
  set(XZ_DECODERS "lzma1;lzma2" CACHE STRING "Private codec filters" FORCE)
  set(XZ_MATCH_FINDERS bt4 CACHE STRING "Preset 9 extreme match finder" FORCE)
  set(XZ_CHECKS crc32 CACHE STRING "CRC32 for Fast XZ; regular raw streams use no container check" FORCE)
  add_subdirectory(third_party/xz EXCLUDE_FROM_ALL)
  get_target_property(lzma_type liblzma TYPE)
  if(NOT lzma_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "DataPump requires its vendored static liblzma")
  endif()
  if(DATAPUMP_SANITIZERS AND NOT MSVC)
    # The bounded codec has intensive C loops too; use the same instrumented
    # optimization level as the C++ callers while preserving Debug symbols.
    target_compile_options(liblzma PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(liblzma PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
datapump_add_lzma()
