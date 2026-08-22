cmake_minimum_required(VERSION 3.13)

include_directories(
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")
add_compile_options(-pthread)

add_library(platform_mutex mutex/WasmMutex.cpp)
target_include_directories(platform_mutex PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_timer timer/WasmTimer.cpp)
target_include_directories(platform_timer PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_filesystem
  filesystem/WasmFile.cpp
  filesystem/WasmFileSystem.cpp)
target_include_directories(platform_filesystem PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_audio audio/WasmSilentAudio.cpp)
target_include_directories(platform_audio PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_midi midi/WasmDisconnectedMidi.cpp)
target_include_directories(platform_midi PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform process/WasmProcess.cpp)
target_include_directories(platform PUBLIC
  "${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")
target_link_libraries(platform PUBLIC platform_mutex)

add_library(platform_system system/WasmSystem.cpp)
target_include_directories(platform_system PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_system PUBLIC
  platform_audio
  platform_filesystem
  platform_midi
  platform_timer
  platform)

# Task 4 supplies the actual SDL2 display and GUI factory. The core view
# libraries only need the platform display target to exist while Task 3 proves
# non-device link closure; this interface target provides no GUI implementation.
add_library(platform_display INTERFACE)

add_subdirectory("${PROJECT_SOURCE_DIR}/System"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/System")
add_subdirectory("${PROJECT_SOURCE_DIR}/Foundation"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Foundation")
add_subdirectory("${PROJECT_SOURCE_DIR}/Externals"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Externals")
# Emscripten's C compiler does not provide NULL transitively to the existing
# sinc stub. Keep this compatibility include local to the browser target.
target_compile_options(samplerate PRIVATE -include stddef.h)
add_subdirectory("${PROJECT_SOURCE_DIR}/Services"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Services")
add_subdirectory("${PROJECT_SOURCE_DIR}/UIFramework"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/UIFramework")
add_subdirectory("${PROJECT_SOURCE_DIR}/Application"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Application")
foreach(target application_model application_views)
  target_compile_options(${target} PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/wasm_compat.h")
endforeach()
target_compile_options(application_instruments PRIVATE -Wno-non-pod-varargs)
target_compile_options(application_views PRIVATE -Wno-c++11-narrowing)
target_compile_options(application_views_modaldialogs PRIVATE
  -Wno-c++11-narrowing)

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
  main/core_link_closure.cpp
  main/main.cpp
  platform/wasm_bridge.cpp)
set_source_files_properties(main/core_link_closure.cpp PROPERTIES
  COMPILE_OPTIONS
    "-Wno-error;-Wno-unused-parameter;-Wno-deprecated-declarations;-Wno-inconsistent-missing-override")

target_include_directories(picotracker_wasm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(picotracker_wasm PRIVATE
  platform_system
  -Wl,--whole-archive
  application
  -Wl,--no-whole-archive
  services_audio
  platform_filesystem
  platform_midi
  platform_timer
  platform
  system_timer
  foundation)
target_compile_definitions(picotracker_wasm PRIVATE
  PICOTRACKER_WASM_BUILD_COMMIT="${PICOTRACKER_WASM_COMMIT}"
  PICOTRACKER_WASM_BUILD_DIRTY=${PICOTRACKER_WASM_DIRTY}
  PICOTRACKER_WASM_BUILD_TIME="${PICOTRACKER_WASM_BUILT_AT}")
target_compile_options(picotracker_wasm PRIVATE -pthread -Wall -Wextra -Werror)
target_link_options(picotracker_wasm PRIVATE
  -pthread
  "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/picotracker-core-link.map"
  -sERROR_ON_UNDEFINED_SYMBOLS=1
  -sPROXY_TO_PTHREAD=1
  -sPTHREAD_POOL_SIZE=4
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createPicoTrackerModule
  -sINITIAL_MEMORY=268435456
  -sENVIRONMENT=web,worker
  "-sEXPORTED_FUNCTIONS=['_main','_PicoTracker_Wasm_GetBuildMetadataJson','_PicoTracker_Wasm_GetState','_PicoTracker_Wasm_RequestShutdown','_PicoTracker_Wasm_GetLastError','_PicoTracker_Wasm_CoreApplicationAnchor','_PicoTracker_Wasm_CoreAppWindowAnchor','_PicoTracker_Wasm_CorePlayerAnchor','_PicoTracker_Wasm_CoreViewAnchor']"
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','PThread']")

set(PICOTRACKER_WEB_WASM_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../web/public/wasm"
  CACHE PATH "Output directory consumed by the Vite workbench")
set_target_properties(picotracker_wasm PROPERTIES
  OUTPUT_NAME picotracker
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${PICOTRACKER_WEB_WASM_DIR}")

add_custom_command(TARGET picotracker_wasm POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    "-DJS_GLUE=${PICOTRACKER_WEB_WASM_DIR}/picotracker.js"
    "-DWASM_BINARY=${PICOTRACKER_WEB_WASM_DIR}/picotracker.wasm"
    "-DLINK_MAP=${CMAKE_CURRENT_BINARY_DIR}/picotracker-core-link.map"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCoreSymbols.cmake"
  COMMENT "Verifying non-GUI PicoTracker core link closure"
  VERBATIM)
