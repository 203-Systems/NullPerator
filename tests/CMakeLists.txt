cmake_minimum_required(VERSION 3.13)

project(picoTrackerHostTests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(picoTracker_tests
  test_main.cpp
  foundation_types_tests.cpp
  wavheader_tests.cpp
  wasm_audio_tests.cpp
  wasm_application_snapshot_tests.cpp
  wasm_platform_tests.cpp
  ../sources/Adapters/wasm/audio/AudioRenderOracle.cpp
  ../sources/Adapters/wasm/audio/AudioWorklet.cpp
  ../sources/Adapters/wasm/audio/WasmAudioDriver.cpp
  ../sources/Adapters/wasm/audio/PcmRingBuffer.cpp
  ../sources/Adapters/wasm/audio/OutputResampler.cpp
  ../sources/Adapters/wasm/platform/WasmApplicationSnapshot.cpp
  ../sources/Services/Audio/AudioDriver.cpp
  ../sources/Adapters/wasm/input/InputMap.cpp
  ../sources/Application/Instruments/WavHeader.cpp
  ../sources/Adapters/wasm/system/WasmSystem.cpp
  ../sources/Adapters/wasm/timer/WasmTimer.cpp
  ../sources/Adapters/wasm/filesystem/WasmFile.cpp
  ../sources/Adapters/wasm/filesystem/WasmFileSystem.cpp
  ../sources/Adapters/wasm/filesystem/WasmStorageBridge.cpp
  ../sources/Foundation/Observable.cpp
  ../sources/System/Timer/Timer.cpp
)

target_include_directories(picoTracker_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(picoTracker_tests PRIVATE
  HOST_TEST
)
