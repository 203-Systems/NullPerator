cmake_minimum_required(VERSION 3.13)

include_directories(
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")

# The browser simulator models the 240x240 Node/Operator target.  Keep the
# application-side 30-column layout identical to the firmware instead of
# compiling the legacy 320x240 picoTracker layout and squeezing it later.
add_compile_definitions(NODE)
set(PICOTRACKER_UI "ui2" CACHE STRING
  "Browser UI product (UI2 only)" FORCE)
add_compile_definitions(PICOTRACKER_UI2_PRODUCT=1)

set(PICOTRACKER_WEB_WASM_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../web/public/wasm"
  CACHE PATH "Output directory consumed by the Vite workbench")
# Enter before the product's directory-wide pthread option so this isolated
# numeric fixture is a genuine no-pthread Emscripten module.
add_subdirectory(audio/oracle)

add_compile_options(-pthread)

add_library(platform_mutex mutex/WasmMutex.cpp)
target_include_directories(platform_mutex PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_timer timer/WasmTimer.cpp)
target_include_directories(platform_timer PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_filesystem
  filesystem/WasmFile.cpp
  filesystem/WasmFileSystem.cpp
  filesystem/WasmStorageBridge.cpp)
target_include_directories(platform_filesystem PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_snapshot
  platform/WasmApplicationSnapshot.cpp)
target_include_directories(platform_snapshot PUBLIC "${PROJECT_SOURCE_DIR}")

add_subdirectory("${PROJECT_SOURCE_DIR}/UI2"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/UI2")

add_library(platform_audio
  audio/AudioRenderOracle.cpp
  audio/AudioWorklet.cpp
  audio/OutputResampler.cpp
  audio/PcmRingBuffer.cpp
  audio/WasmAudio.cpp
  audio/WasmAudioDriver.cpp)
target_include_directories(platform_audio PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_audio PUBLIC platform_snapshot)

add_library(platform_midi
  midi/MidiByteQueue.cpp
  midi/MidiLatencyTrace.cpp
  midi/WasmMidiInDevice.cpp
  midi/WasmMidiOutDevice.cpp
  midi/WasmMidiService.cpp)
target_include_directories(platform_midi PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_logging logging/WasmTrace.cpp)
target_include_directories(platform_logging PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_tracing
  tracing/InputFrameLatencyTracker.cpp
  tracing/TraceRingBuffer.cpp
  tracing/WasmProfiler.cpp
  tracing/Benchmark.cpp)
target_include_directories(platform_tracing PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform process/WasmProcess.cpp)
target_include_directories(platform PUBLIC
  "${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")
target_link_libraries(platform PUBLIC platform_mutex)

add_library(platform_system
  system/WasmSamplePool.cpp
  system/WasmSystem.cpp)
target_include_directories(platform_system PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_system PUBLIC
  platform_audio
  platform_filesystem
  platform_logging
  platform_tracing
  platform_midi
  platform_timer
  platform)

add_library(platform_display
  gui/GUIFactory.cpp
  gui/WasmEventManager.cpp
  gui/WasmGUIWindowImp.cpp
  gui/WasmUiPresenter.cpp)
target_include_directories(platform_display PUBLIC "${PROJECT_SOURCE_DIR}")
target_compile_options(platform_display PRIVATE --use-port=sdl2)
target_link_libraries(platform_display PUBLIC platform_snapshot platform_tracing ui2)

add_subdirectory("${PROJECT_SOURCE_DIR}/System"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/System")
# The workbench's Logs panel is a production feature. Keep only Trace.cpp's
# existing log calls active in Release WASM builds without changing the rest
# of the firmware's NDEBUG/assertion policy.
target_compile_options(system_console PRIVATE -UNDEBUG)
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
  if(TARGET ${target})
    target_compile_options(${target} PRIVATE
      -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/wasm_compat.h")
  endif()
endforeach()
# Passing an ETL string object through a printf-style ellipsis compiles to a
# runtime trap in Clang/WASM. Keep this fatal so a future call cannot silently
# become `RuntimeError: unreachable` in the browser.
target_compile_options(application_instruments PRIVATE -Werror=non-pod-varargs)
if(TARGET application_views)
  target_compile_options(application_views PRIVATE -Wno-c++11-narrowing)
endif()
if(TARGET application_views_modaldialogs)
  target_compile_options(application_views_modaldialogs PRIVATE
    -Wno-c++11-narrowing)
endif()

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
  input/InputMap.cpp
  main/main.cpp
  platform/wasm_bridge.cpp)
set_source_files_properties(main/main.cpp PROPERTIES
  COMPILE_OPTIONS
    "-Wno-error;-Wno-unused-parameter;-Wno-deprecated-declarations")

target_include_directories(picotracker_wasm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(picotracker_wasm PRIVATE
  platform_system
  platform_display
  -Wl,--whole-archive
  application_ui2
  -Wl,--no-whole-archive
  services_audio
  platform_filesystem
  platform_midi
  platform_timer
  platform
  system_timer
  uiframework_simplebaseclasses
  uiframework_basicdatas
  foundation)
target_compile_definitions(picotracker_wasm PRIVATE
  PICOTRACKER_WASM_BUILD_COMMIT="${PICOTRACKER_WASM_COMMIT}"
  PICOTRACKER_WASM_BUILD_DIRTY=${PICOTRACKER_WASM_DIRTY}
  PICOTRACKER_WASM_BUILD_TIME="${PICOTRACKER_WASM_BUILT_AT}")
target_compile_options(picotracker_wasm PRIVATE
  -pthread
  --use-port=sdl2
  -Wall
  -Wextra
  -Werror)
target_link_options(picotracker_wasm PRIVATE
  -pthread
  -lidbfs.js
  --use-port=sdl2
  "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/picotracker-core-link.map"
  -sERROR_ON_UNDEFINED_SYMBOLS=1
  -sPROXY_TO_PTHREAD=1
  -sOFFSCREENCANVAS_SUPPORT=1
  "-sOFFSCREENCANVASES_TO_PTHREAD=#picotracker-canvas"
  -sPTHREAD_POOL_SIZE=4
  -sWASM_WORKERS=1
  -sAUDIO_WORKLET=1
  $<$<CONFIG:Debug>:-sWEBAUDIO_DEBUG=1>
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createPicoTrackerModule
  -sINITIAL_MEMORY=268435456
  -sENVIRONMENT=web,worker,worklet
  "-sEXPORTED_FUNCTIONS=['_main','_PicoTracker_Wasm_GetBuildMetadataJson','_PicoTracker_Wasm_GetState','_PicoTracker_Wasm_BootstrapAudio','_PicoTracker_Wasm_MarkAudioUnavailable','_PicoTracker_Wasm_ConfigureAudio','_PicoTracker_Wasm_RequestShutdown','_PicoTracker_Wasm_GetLastError','_PicoTracker_Wasm_DiagnosticFail','_PicoTracker_Wasm_SetAction','_PicoTracker_Wasm_ReleaseAllActions','_PicoTracker_Wasm_GetActionMask','_PicoTracker_Wasm_GetActionGeneration','_PicoTracker_Wasm_GetLastAction','_PicoTracker_Wasm_RequestDiagnosticView','_PicoTracker_Wasm_GetDiagnosticView','_PicoTracker_Wasm_GetDiagnosticViewGeneration','_PicoTracker_Wasm_GetDiagnosticInputGeneration','_PicoTracker_Wasm_RequestDiagnosticModal','_PicoTracker_Wasm_GetDiagnosticModal','_PicoTracker_Wasm_GetDiagnosticModalGeneration','_PicoTracker_Wasm_CaptureFrameRgba','_PicoTracker_Wasm_GetFrameSnapshotSequence','_PicoTracker_Wasm_UnlockAudio','_PicoTracker_Wasm_StopAudio','_PicoTracker_Wasm_GetAudioState','_PicoTracker_Wasm_GetAudioError','_PicoTracker_Wasm_GetAudioMetrics','_PicoTracker_Wasm_GetAudioMetricsSnapshot','_PicoTracker_Wasm_GetAudioErrorSnapshot','_PicoTracker_Wasm_GetBrowserSnapshots','_PicoTracker_Wasm_MidiInputBuffer','_PicoTracker_Wasm_MidiInputCapacity','_PicoTracker_Wasm_MidiInput','_PicoTracker_Wasm_MidiDrainOutput','_PicoTracker_Wasm_MidiDisconnect','_PicoTracker_Wasm_MidiSetOutputConnected','_PicoTracker_Wasm_MidiDiagnosticSnapshot','_PicoTracker_Wasm_MidiDiagnosticOutput','_PicoTracker_Wasm_LogDrain','_PicoTracker_Wasm_TraceStart','_PicoTracker_Wasm_TraceStop','_PicoTracker_Wasm_TraceDrain','_PicoTracker_Wasm_TraceStorageSync','_PicoTracker_Wasm_RunBenchmark']"
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPU8','HEAPU32','PThread','FS','IDBFS','addRunDependency','removeRunDependency']")

set_target_properties(picotracker_wasm PROPERTIES
  OUTPUT_NAME picotracker
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${PICOTRACKER_WEB_WASM_DIR}")


add_custom_command(TARGET picotracker_wasm POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    "-DWASM_BINARY=${PICOTRACKER_WEB_WASM_DIR}/picotracker.wasm"
    "-DLINK_MAP=${CMAKE_CURRENT_BINARY_DIR}/picotracker-core-link.map"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCoreSymbols.cmake"
  COMMENT "Verifying non-GUI PicoTracker core link closure"
  VERBATIM)
