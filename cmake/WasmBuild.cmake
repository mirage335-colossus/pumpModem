include_guard(GLOBAL)
if(NOT DATAPUMP_BUILD_WASM)
  return()
endif()
if(NOT EMSCRIPTEN OR NOT DATAPUMP_WASM_SDK_ROOT)
  message(FATAL_ERROR "Wasm requires the explicitly prepared SDK and cmake/toolchains/wasm-sdk.cmake")
endif()
if(DATAPUMP_BUILD_GUI OR DATAPUMP_BUILD_TUI OR DATAPUMP_BUILD_FB OR DATAPUMP_BUILD_FRAMEBUFFER_LIBRARY OR DATAPUMP_BUILD_CLI OR DATAPUMP_BUILD_WEB_WORKER OR DATAPUMP_SDK_ROOT)
  message(FATAL_ERROR "Wasm uses its own build tree; native applications and the Linux source SDK cannot be mixed into it")
endif()
if(DATAPUMP_SANITIZERS)
  message(FATAL_ERROR "Use native sanitizer qualification; this Wasm profile is not a sanitizer profile")
endif()
# Cooperative C++ fibers need Asyncify, never pthreads/shared memory or a proxy.
add_compile_options(-fexceptions)
add_link_options(-fexceptions -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=262144 -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=1
  -sENVIRONMENT=web,worker -sMODULARIZE=1 -sEXPORT_NAME=createDataPump
  -sINCOMING_MODULE_JS_API=wasmBinary,instantiateWasm,print,printErr,onRuntimeInitialized,noInitialRun,locateFile,onAbort
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8
  -sNO_EXIT_RUNTIME=1 -sASSERTIONS=1 -sDYNAMIC_EXECUTION=0
  -sEXPORTED_FUNCTIONS=_malloc,_free,_datapump_web_create,_datapump_web_feed,_datapump_web_tick,_datapump_web_output,_datapump_web_output_size,_datapump_web_consume,_datapump_web_destroy)
