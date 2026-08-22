cmake_minimum_required(VERSION 3.13)

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
  OUTPUT_VARIABLE PICOTRACKER_WASM_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT PICOTRACKER_WASM_COMMIT)
  set(PICOTRACKER_WASM_COMMIT "unknown")
endif()

execute_process(
  COMMAND git status --porcelain
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
  OUTPUT_VARIABLE PICOTRACKER_WASM_DIRTY_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(PICOTRACKER_WASM_DIRTY_OUTPUT)
  set(PICOTRACKER_WASM_DIRTY true)
else()
  set(PICOTRACKER_WASM_DIRTY false)
endif()

string(TIMESTAMP PICOTRACKER_WASM_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)

add_executable(picotracker_wasm
  main/main.cpp
  platform/wasm_bridge.cpp)

target_include_directories(picotracker_wasm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(picotracker_wasm PRIVATE
  PICOTRACKER_WASM_BUILD_COMMIT="${PICOTRACKER_WASM_COMMIT}"
  PICOTRACKER_WASM_BUILD_DIRTY=${PICOTRACKER_WASM_DIRTY}
  PICOTRACKER_WASM_BUILD_TIME="${PICOTRACKER_WASM_BUILT_AT}")
target_compile_options(picotracker_wasm PRIVATE -pthread -Wall -Wextra -Werror)
target_link_options(picotracker_wasm PRIVATE
  -pthread
  -sPROXY_TO_PTHREAD=1
  -sPTHREAD_POOL_SIZE=4
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createPicoTrackerModule
  -sINITIAL_MEMORY=268435456
  -sENVIRONMENT=web,worker
  "-sEXPORTED_FUNCTIONS=['_main','_PicoTracker_Wasm_GetBuildMetadataJson','_PicoTracker_Wasm_GetState','_PicoTracker_Wasm_RequestShutdown','_PicoTracker_Wasm_GetLastError']"
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','PThread']")

set(PICOTRACKER_WEB_WASM_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../web/public/wasm"
  CACHE PATH "Output directory consumed by the Vite workbench")
set_target_properties(picotracker_wasm PROPERTIES
  OUTPUT_NAME picotracker
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${PICOTRACKER_WEB_WASM_DIR}")
