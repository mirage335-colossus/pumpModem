cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS "SDK ELF packaging fixture applies only to Linux")
  return()
endif()
if(NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "Provide -DBINARY_DIR for the SDK packaging fixture")
endif()
get_filename_component(BINARY_DIR "${BINARY_DIR}" ABSOLUTE)
if(C_COMPILER)
  set(compiler "${C_COMPILER}")
else()
  find_program(compiler NAMES cc gcc clang REQUIRED)
endif()
if(OBJDUMP)
  set(objdump "${OBJDUMP}")
else()
  find_program(objdump NAMES objdump REQUIRED)
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef identifier)
set(scratch "${BINARY_DIR}/${identifier}")
set(sdk "${scratch}/SDK with spaces")
set(sysroot "${sdk}/sysroot")
set(host "${scratch}/host libraries")
set(installation "${scratch}/application")
file(MAKE_DIRECTORY "${sysroot}/usr/lib" "${sdk}/share/datapump-sdk/licenses/fixture"
  "${host}" "${installation}/bin" "${scratch}/host tools")
file(WRITE "${sdk}/share/datapump-sdk/manifest.json" "{\"fixture\":true}\n")
file(WRITE "${sdk}/share/datapump-sdk/licenses/fixture/COPYING"
  "SDK fixture notice; /usr/share/common-licenses/GPL-2 is text, not a host input.\n")
file(WRITE "${scratch}/leaf.c" "int sdk_leaf(void) { return 0; }\n")
file(WRITE "${scratch}/host-leaf.c" "int sdk_leaf(void) { return 37; }\n")
file(WRITE "${scratch}/middle.c" "extern int sdk_leaf(void); int sdk_middle(void) { return sdk_leaf(); }\n")
file(WRITE "${scratch}/main.c" "extern int sdk_middle(void); int main(void) { return sdk_middle(); }\n")
file(WRITE "${sysroot}/usr/lib/libstatic-fixture.a" "!<arch>\n")
file(WRITE "${host}/libstatic-fixture.a" "!<arch>\n")
function(compile_fixture)
  execute_process(COMMAND "${compiler}" ${ARGN} RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT status STREQUAL "0")
    message(FATAL_ERROR "SDK packaging fixture compilation failed: ${output}\n${error}")
  endif()
endfunction()
compile_fixture(-fPIC -shared -Wl,-soname,libsdk_leaf.so.1 "${scratch}/leaf.c"
  -o "${sysroot}/usr/lib/libsdk_leaf.so.1.0")
compile_fixture(-fPIC -shared -Wl,-soname,libsdk_leaf.so.1 "${scratch}/host-leaf.c"
  -o "${host}/libsdk_leaf.so.1.0")
file(CREATE_LINK libsdk_leaf.so.1.0 "${sysroot}/usr/lib/libsdk_leaf.so.1" SYMBOLIC)
file(CREATE_LINK libsdk_leaf.so.1.0 "${host}/libsdk_leaf.so.1" SYMBOLIC)
compile_fixture(-fPIC -shared -Wl,-soname,libsdk_middle.so.1 "${scratch}/middle.c"
  "${sysroot}/usr/lib/libsdk_leaf.so.1.0" "-Wl,-rpath,${host}"
  -o "${sysroot}/usr/lib/libsdk_middle.so.1.0")
file(CREATE_LINK libsdk_middle.so.1.0 "${sysroot}/usr/lib/libsdk_middle.so.1" SYMBOLIC)
file(COPY "${sysroot}/usr/lib/libsdk_middle.so.1.0" DESTINATION "${host}")
file(CREATE_LINK libsdk_middle.so.1.0 "${host}/libsdk_middle.so.1" SYMBOLIC)
compile_fixture("${scratch}/main.c" "${host}/libsdk_middle.so.1.0" "-Wl,-rpath-link,${host}"
  -Wl,--disable-new-dtags "-Wl,-rpath,$ORIGIN/../lib:${host}" -o "${installation}/bin/pump")

# Exercise the actual configured install script, including notices and copying.
set(DATAPUMP_RUNTIME_PLATFORM linux+elf)
set(DATAPUMP_RUNTIME_TOOL objdump)
set(DATAPUMP_RUNTIME_COMMAND "${objdump}")
set(DATAPUMP_POLICY_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/RuntimePolicy.cmake")
set(DATAPUMP_SDK_RUNTIME_HELPER "${CMAKE_CURRENT_LIST_DIR}/../cmake/SdkRuntime.cmake")
set(DATAPUMP_EXECUTABLE_NAMES pump)
set(DATAPUMP_SEARCH_DIRS "${host}")
set(DATAPUMP_PACKAGE_SDK_ROOT "${sdk}")
set(DATAPUMP_PACKAGE_SYSROOT "${sysroot}")
set(DATAPUMP_SDK_EXTERNAL_LIBRARIES "${sysroot}/usr/lib/libstatic-fixture.a")
set(PROJECT_VERSION fixture)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../tools/package-native.cmake"
  "${scratch}/package.cmake" @ONLY)
file(WRITE "${scratch}/host tools/dpkg-query" "#!/bin/sh\n: > \"$DATAPUMP_DPKG_PROBE\"\nexit 1\n")
file(CHMOD "${scratch}/host tools/dpkg-query" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
function(package_fixture expected_error)
  file(REMOVE_RECURSE "${installation}/lib" "${installation}/share")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=${scratch}/host tools:$ENV{PATH}"
    "DATAPUMP_DPKG_PROBE=${scratch}/host-dpkg-was-called"
    "LD_LIBRARY_PATH=${host}" "${CMAKE_COMMAND}" "-DCMAKE_INSTALL_PREFIX=${installation}"
    -P "${scratch}/package.cmake" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(expected_error)
    if(status STREQUAL "0" OR NOT error MATCHES "${expected_error}")
      message(FATAL_ERROR "SDK packaging did not reject ${expected_error}: ${output}\n${error}")
    endif()
  elseif(NOT status STREQUAL "0")
    message(FATAL_ERROR "SDK packaging failed: ${output}\n${error}")
  endif()
endfunction()
package_fixture("")
file(SHA256 "${sysroot}/usr/lib/libsdk_leaf.so.1.0" expected)
file(SHA256 "${installation}/lib/libsdk_leaf.so.1" actual)
if(NOT expected STREQUAL actual)
  message(FATAL_ERROR "SDK packaging selected the host's same-SONAME library")
endif()
execute_process(COMMAND "${installation}/bin/pump" RESULT_VARIABLE ran)
if(NOT ran STREQUAL "0")
  message(FATAL_ERROR "Packaged SDK fixture used a host dependency: ${ran}")
endif()
set(notices "${installation}/share/doc/datapump/runtime-notices")
if(NOT EXISTS "${notices}/sdk/manifest.json" OR NOT EXISTS "${notices}/sdk/licenses/fixture/COPYING")
  message(FATAL_ERROR "SDK packaging omitted the manifest or source notices")
endif()
if(EXISTS "${scratch}/host-dpkg-was-called" OR EXISTS "${notices}/license-texts")
  message(FATAL_ERROR "SDK packaging consulted host package/license metadata")
endif()

# Exercise Portable.cmake's selection separately from the synthetic ELF build.
# Set the fake sysroot after the host compiler check: this is a packaging fixture,
# not a complete libc development tree. A stale native ALSA cache must not leak.
file(WRITE "${scratch}/select-sdk.cmake"
  "set(DATAPUMP_SDK_ROOT [==[${sdk}]==])\nset(CMAKE_SYSROOT [==[${sysroot}]==])\n"
  "set(DATAPUMP_ALSA_LIBRARY [==[${host}/libsdk_leaf.so.1.0]==] CACHE FILEPATH fixture FORCE)\n")
file(MAKE_DIRECTORY "${scratch}/configure-source")
file(WRITE "${scratch}/configure-source/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.21)\nproject(SdkPackagingFixture VERSION 1.0 LANGUAGES C)\n"
  "set(DATAPUMP_PORTABLE ON)\nadd_executable(pump [==[${scratch}/main.c]==])\n"
  "if(ZLIB_FIXTURE)\n"
  "  add_library(ZLIB::ZLIB STATIC IMPORTED)\n"
  "  set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION \"\${ZLIB_FIXTURE}\")\n"
  "elseif(VENDORED_ZLIB_FIXTURE)\n"
  "  add_library(fixture-zlib STATIC [==[${scratch}/leaf.c]==])\n"
  "  add_library(ZLIB::ZLIB ALIAS fixture-zlib)\n"
  "endif()\n"
  "include([==[${CMAKE_CURRENT_LIST_DIR}/../cmake/Portable.cmake]==])\n"
  "datapump_install_native(TARGETS pump)\n")
function(configure_fixture name expected_error)
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${scratch}/configure-source"
    -B "${scratch}/${name}" "-DCMAKE_PROJECT_INCLUDE=${scratch}/select-sdk.cmake"
    "-DCMAKE_C_COMPILER=${compiler}" "-DCMAKE_OBJDUMP=${objdump}" ${ARGN}
    RESULT_VARIABLE configured OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(expected_error)
    if(configured STREQUAL "0" OR NOT error MATCHES "${expected_error}")
      message(FATAL_ERROR "SDK configuration did not reject ${expected_error}: ${output}\n${error}")
    endif()
  elseif(NOT configured STREQUAL "0")
    message(FATAL_ERROR "SDK packaging configuration failed: ${output}\n${error}")
  endif()
endfunction()
configure_fixture(configure ""
  "-DOPENSSL_CRYPTO_LIBRARY=${sysroot}/usr/lib/libstatic-fixture.a"
  "-DZLIB_FIXTURE=${sysroot}/usr/lib/libstatic-fixture.a")
file(READ "${scratch}/configure/package-native-configured.cmake" configured_script)
string(FIND "${configured_script}" "set(optional_alsa [==[]==])" omitted_host_alsa)
string(FIND "${configured_script}" "set(sdk_root [==[${sdk}]==])" selected_sdk)
if(omitted_host_alsa EQUAL -1 OR selected_sdk EQUAL -1)
  message(FATAL_ERROR "SDK packaging configuration retained native ALSA or omitted its SDK")
endif()
configure_fixture(host-openssl "SDK path escapes its root"
  "-DOPENSSL_CRYPTO_LIBRARY=${host}/libstatic-fixture.a")
configure_fixture(host-zlib "SDK path escapes its root"
  "-DZLIB_FIXTURE=${host}/libstatic-fixture.a")
configure_fixture(vendored-zlib "" -DVENDORED_ZLIB_FIXTURE=ON)

# Explicit static input paths need checking at install time as well: those
# archives cannot be discovered from the executable's dynamic dependencies.
set(DATAPUMP_SDK_EXTERNAL_LIBRARIES "${host}/libstatic-fixture.a")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../tools/package-native.cmake"
  "${scratch}/package.cmake" @ONLY)
package_fixture("SDK path escapes its root")
set(DATAPUMP_SDK_EXTERNAL_LIBRARIES "${sysroot}/usr/lib/libstatic-fixture.a")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../tools/package-native.cmake"
  "${scratch}/package.cmake" @ONLY)

file(REMOVE "${sysroot}/usr/lib/libsdk_leaf.so.1")
file(RENAME "${sysroot}/usr/lib/libsdk_leaf.so.1.0" "${scratch}/saved-sdk-leaf")
package_fixture("Missing SDK runtime dependency libsdk_leaf.so.1")
file(CREATE_LINK "${host}/libsdk_leaf.so.1.0" "${sysroot}/usr/lib/libsdk_leaf.so.1" SYMBOLIC)
package_fixture("SDK path escapes its root")
file(REMOVE "${sysroot}/usr/lib/libsdk_leaf.so.1")
file(RENAME "${scratch}/saved-sdk-leaf" "${sysroot}/usr/lib/libsdk_leaf.so.1.0")
file(CREATE_LINK libsdk_leaf.so.1.0 "${sysroot}/usr/lib/libsdk_leaf.so.1" SYMBOLIC)
file(REMOVE "${sdk}/share/datapump-sdk/licenses/fixture/COPYING")
file(CREATE_LINK "${scratch}/leaf.c" "${sdk}/share/datapump-sdk/licenses/fixture/COPYING" SYMBOLIC)
package_fixture("SDK path escapes its root")
message(STATUS "SDK packaging isolation, transitive dependency, symlink and notice checks passed")
