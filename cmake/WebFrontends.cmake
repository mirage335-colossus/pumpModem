# Socket-free compositions are deliberately declared after native audio users.
if(NOT DATAPUMP_BUILD_WEB_WORKER AND NOT DATAPUMP_BUILD_WASM)
  return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/GuiShared.cmake")
add_library(datapump_host_audio OBJECT src/host/audio.cpp)
target_link_libraries(datapump_host_audio PRIVATE datapump)
add_library(datapump_web_bridge STATIC src/gui/web_bridge.cpp)
target_include_directories(datapump_web_bridge PUBLIC src/gui)
target_link_libraries(datapump_web_bridge PRIVATE datapump_gui_application)
if(DATAPUMP_BUILD_WEB_WORKER)
  add_executable(datapump-worker src/host/worker.cpp src/host/runtime.cpp src/host/protocol.cpp src/host/sandbox.cpp)
  target_link_libraries(datapump-worker PRIVATE datapump_web_bridge datapump_host_audio datapump)
  target_compile_definitions(datapump-worker PRIVATE DATAPUMP_VERSION="${DATAPUMP_VERSION}")
  list(APPEND datapump_executables datapump-worker)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/web-worker-manifest.json"
    CONTENT "{\"schema_version\":1,\"target\":\"datapump-worker\",\"protocol\":\"DPW1\",\"audio\":\"browser\",\"files\":\"host\",\"transport\":\"inherited-anonymous-pipes\",\"socket_policy\":\"forbidden\",\"unsupported_platforms\":\"fail-closed\"}\n")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/web-worker-manifest.json"
    DESTINATION share/datapump/web/hosted RENAME web-manifest.json)
  install(DIRECTORY web/ DESTINATION share/datapump/web/hosted
    PATTERN "*.md" EXCLUDE)
endif()
if(DATAPUMP_BUILD_WASM)
  add_executable(datapump-wasm src/host/wasm.cpp src/host/runtime.cpp src/host/protocol.cpp)
  target_link_libraries(datapump-wasm PRIVATE datapump_web_bridge datapump_host_audio datapump)
  target_compile_definitions(datapump-wasm PRIVATE DATAPUMP_VERSION="${DATAPUMP_VERSION}")
  set_target_properties(datapump-wasm PROPERTIES SUFFIX ".js")
  list(APPEND datapump_executables datapump-wasm)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_wasm_page "${CMAKE_CURRENT_BINARY_DIR}/web/datapump-wasm.html")
  set(_wasm_assets renderer.mjs protocol.mjs browser_audio.mjs wasm_client.mjs
    wasm_worker.js audio_worklet.js style.css)
  list(TRANSFORM _wasm_assets PREPEND "${CMAKE_SOURCE_DIR}/web/")
  file(GLOB _wasm_notices "${DATAPUMP_WASM_SDK_ROOT}/share/datapump-wasm-sdk/licenses/*")
  list(APPEND _wasm_notices "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/third_party/qrcodegen/LICENSE"
    "${CMAKE_SOURCE_DIR}/third_party/xz/COPYING.0BSD"
    "${CMAKE_SOURCE_DIR}/third_party/ldpc/LICENSE" "${CMAKE_SOURCE_DIR}/third_party/ldpc/WIFI-LICENSE")
  add_custom_command(OUTPUT "${_wasm_page}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/package-wasm.py"
      --javascript "$<TARGET_FILE:datapump-wasm>"
      --wasm "${CMAKE_CURRENT_BINARY_DIR}/datapump-wasm.wasm"
      --sdk-notices "${DATAPUMP_WASM_SDK_ROOT}/share/datapump-wasm-sdk/licenses"
      --assets "${CMAKE_SOURCE_DIR}/web" --output "${CMAKE_CURRENT_BINARY_DIR}/web"
    DEPENDS datapump-wasm ${_wasm_assets} ${_wasm_notices} "${CMAKE_SOURCE_DIR}/tools/package-wasm.py"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/web/web-manifest.json" "${CMAKE_CURRENT_BINARY_DIR}/web/manifest.sha256"
    COMMENT "Auditing and packaging the self-contained browser application" VERBATIM)
  add_custom_target(datapump-wasm-page ALL DEPENDS "${_wasm_page}")
  list(APPEND datapump_app_dependencies datapump-wasm-page)
  if(BUILD_TESTING)
    find_program(DATAPUMP_NODE_EXECUTABLE node HINTS "${DATAPUMP_WASM_SDK_ROOT}/node/bin" REQUIRED NO_DEFAULT_PATH)
    add_test(NAME wasm_runtime COMMAND "${DATAPUMP_NODE_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tests/test_wasm_runtime.mjs"
      "$<TARGET_FILE:datapump-wasm>" "${CMAKE_CURRENT_BINARY_DIR}/datapump-wasm.wasm")
    set_tests_properties(wasm_runtime PROPERTIES LABELS "web" TIMEOUT 240)
    add_test(NAME wasm_live COMMAND "${DATAPUMP_NODE_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tests/test_web_live.mjs" --wasm
      "$<TARGET_FILE:datapump-wasm>" "${CMAKE_CURRENT_BINARY_DIR}/datapump-wasm.wasm")
    set_tests_properties(wasm_live PROPERTIES LABELS "web" TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME browser_audio COMMAND "${DATAPUMP_NODE_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tests/test_browser_audio.mjs")
    set_tests_properties(browser_audio PROPERTIES LABELS "web" TIMEOUT 30)
    add_executable(test_wasm_entropy third_party/build-support/wasm-sdk/entropy-probe.cpp)
    target_link_libraries(test_wasm_entropy PRIVATE OpenSSL::Crypto)
    target_link_options(test_wasm_entropy PRIVATE --no-entry -sMODULARIZE=1
      -sEXPORT_NAME=DatapumpEntropyProbe -sENVIRONMENT=web,worker
      -sEXPORTED_FUNCTIONS=_datapump_entropy_probe)
    set_target_properties(test_wasm_entropy PROPERTIES SUFFIX ".js")
    add_test(NAME wasm_entropy COMMAND "${DATAPUMP_NODE_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/third_party/build-support/wasm-sdk/entropy-probe.mjs"
      "$<TARGET_FILE:test_wasm_entropy>" "${CMAKE_CURRENT_BINARY_DIR}/test_wasm_entropy.wasm")
    set_tests_properties(wasm_entropy PROPERTIES LABELS "web" TIMEOUT 30)
    # Exercise the actual fiber implementation, including its repeated stack
    # alignment/RAII lifetime cases. This Node harness is never installed.
    add_executable(test_execution_wasm tests/test_execution.cpp src/execution_fibers.cpp)
    target_include_directories(test_execution_wasm PRIVATE include)
    target_compile_definitions(test_execution_wasm PRIVATE DATAPUMP_EXECUTION_FIBERS=1)
    target_link_options(test_execution_wasm PRIVATE -sENVIRONMENT=node -sMODULARIZE=0
      -sEXPORTED_FUNCTIONS=_main -sNO_EXIT_RUNTIME=0 -sASSERTIONS=2)
    set_target_properties(test_execution_wasm PROPERTIES SUFFIX ".js")
    add_test(NAME execution_wasm COMMAND "${DATAPUMP_NODE_EXECUTABLE}" "$<TARGET_FILE:test_execution_wasm>")
    set_tests_properties(execution_wasm PROPERTIES LABELS "web" TIMEOUT 120)
  endif()
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/web/" DESTINATION share/datapump/web/wasm)
  install(FILES "${DATAPUMP_WASM_SDK_ROOT}/share/datapump-wasm-sdk/manifest.json"
    DESTINATION share/doc/datapump RENAME wasm-sdk-manifest.json)
  install(DIRECTORY "${DATAPUMP_WASM_SDK_ROOT}/share/datapump-wasm-sdk/licenses/"
    DESTINATION share/doc/datapump/wasm-runtime-notices)
  install(FILES "${CMAKE_SOURCE_DIR}/third_party/build-support/wasm-sdk/manifest.json"
    DESTINATION share/doc/datapump RENAME wasm-sdk-recipe.json)
endif()
if(BUILD_TESTING AND NOT DATAPUMP_BUILD_WASM)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_no_socket tests/test_no_socket.cpp src/host/sandbox.cpp)
    target_include_directories(test_no_socket PRIVATE include src/host)
    add_test(NAME no_socket COMMAND test_no_socket)
    set_tests_properties(no_socket PROPERTIES LABELS "web")
  endif()
  add_executable(test_host_audio tests/test_host_audio.cpp)
  target_link_libraries(test_host_audio PRIVATE datapump_host_audio datapump)
  add_test(NAME host_audio COMMAND test_host_audio)
  add_executable(test_web_bridge tests/test_web_bridge.cpp)
  target_link_libraries(test_web_bridge PRIVATE datapump_web_bridge datapump_host_audio datapump)
  add_test(NAME web_bridge COMMAND test_web_bridge)
  set_tests_properties(host_audio web_bridge PROPERTIES LABELS "web")
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND AND DATAPUMP_BUILD_WEB_WORKER)
    add_test(NAME web_worker COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_web_worker.py" "$<TARGET_FILE:datapump-worker>")
    set_tests_properties(web_worker PROPERTIES LABELS "web" TIMEOUT 120)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      add_test(NAME web_preview COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_preview.py" --worker "$<TARGET_FILE:datapump-worker>")
      set_tests_properties(web_preview PROPERTIES LABELS "web" TIMEOUT 180)
    endif()
  endif()
  find_program(DATAPUMP_NODE_EXECUTABLE node)
  if(DATAPUMP_NODE_EXECUTABLE)
    add_test(NAME browser_audio COMMAND "${DATAPUMP_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_browser_audio.mjs")
    set_tests_properties(browser_audio PROPERTIES LABELS "web" TIMEOUT 30)
    if(DATAPUMP_BUILD_WEB_WORKER)
      add_test(NAME web_live COMMAND "${DATAPUMP_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_web_live.mjs"
        --native "$<TARGET_FILE:datapump-worker>")
      set_tests_properties(web_live PROPERTIES LABELS "web" TIMEOUT 180 RUN_SERIAL TRUE)
    endif()
    add_test(NAME web_renderer COMMAND "${DATAPUMP_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_web_renderer.mjs")
    set_tests_properties(web_renderer PROPERTIES LABELS "web")
    add_test(NAME web_preview_client COMMAND "${DATAPUMP_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_preview_client.mjs")
    set_tests_properties(web_preview_client PROPERTIES LABELS "web" TIMEOUT 30)
  endif()
endif()
