cmake_minimum_required(VERSION 3.13)

project(picoTrackerHostTests C CXX)

enable_testing()

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
  ../sources/Adapters/wasm/gui/WasmUiPresenter.cpp
  ../sources/Foundation/Observable.cpp
  ../sources/System/Timer/Timer.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Render/UiRgb565Presenter.cpp
  ../sources/UI2/Render/UiVuGradient.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/UiEngine.cpp
  ../sources/UI2/Views/Phrase/UiPhraseView.cpp
  ../sources/UI2/Views/Song/UiSongView.cpp
  ../sources/UI2/Views/Table/UiTableView.cpp
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

add_executable(ui2_snapshot_tool
  ui2_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Render/UiVuGradient.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Song/UiSongView.cpp)

target_include_directories(ui2_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_phrase_snapshot_tool
  ui2_phrase_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Phrase/UiPhraseView.cpp)

target_include_directories(ui2_phrase_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_table_snapshot_tool
  ui2_table_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Table/UiTableView.cpp)

target_include_directories(ui2_table_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
  add_test(NAME ui2_song_snapshot
    COMMAND ui2_snapshot_tool "${CMAKE_CURRENT_BINARY_DIR}/song-actual.ppm")
  add_test(NAME ui2_song_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/song-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/song.png"
      --tolerance-region 219,47,16,153,1)
  set_tests_properties(ui2_song_golden PROPERTIES DEPENDS ui2_song_snapshot)

  foreach(PHRASE_STATE note empty fx number)
    add_test(NAME ui2_phrase_${PHRASE_STATE}_snapshot
      COMMAND ui2_phrase_snapshot_tool "${PHRASE_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/phrase-${PHRASE_STATE}-actual.ppm")
    add_test(NAME ui2_phrase_${PHRASE_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/phrase-${PHRASE_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/phrase-${PHRASE_STATE}.png")
    set_tests_properties(ui2_phrase_${PHRASE_STATE}_golden PROPERTIES
      DEPENDS ui2_phrase_${PHRASE_STATE}_snapshot)
  endforeach()

  function(add_ui2_table_golden TABLE_STATE GOLDEN_NAME)
    add_test(NAME ui2_table_${TABLE_STATE}_snapshot
      COMMAND ui2_table_snapshot_tool "${TABLE_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${GOLDEN_NAME}-actual.ppm")
    add_test(NAME ui2_table_${TABLE_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/${GOLDEN_NAME}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/${GOLDEN_NAME}.png")
    set_tests_properties(ui2_table_${TABLE_STATE}_golden PROPERTIES
      DEPENDS ui2_table_${TABLE_STATE}_snapshot)
  endfunction()
  add_ui2_table_golden(phrase phrase-table)
  add_ui2_table_golden(instrument instrument-table)
  add_ui2_table_golden(phrase-number phrase-table-number)
  add_ui2_table_golden(instrument-number instrument-table-number)
endif()
