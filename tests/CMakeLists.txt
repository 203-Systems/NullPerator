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
  wasm_midi_tests.cpp
  wasm_logging_tests.cpp
  wasm_trace_tests.cpp
  wasm_platform_tests.cpp
  ui2_core_tests.cpp
  ../sources/Adapters/wasm/midi/MidiByteQueue.cpp
  ../sources/Adapters/wasm/midi/MidiLatencyTrace.cpp
  ../sources/Adapters/wasm/midi/WasmMidiOutDevice.cpp
  ../sources/Adapters/wasm/logging/WasmTrace.cpp
  ../sources/Adapters/wasm/tracing/InputFrameLatencyTracker.cpp
  ../sources/Adapters/wasm/tracing/TraceRingBuffer.cpp
  ../sources/Adapters/wasm/tracing/WasmProfiler.cpp
  ../sources/Adapters/wasm/tracing/Benchmark.cpp
  ../sources/Services/Midi/MidiOutDevice.cpp
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
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/UiEngine.cpp
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
