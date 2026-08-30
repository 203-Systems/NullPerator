cmake_minimum_required(VERSION 3.13)

project(picoTrackerHostTests C CXX)

enable_testing()

option(PICOTRACKER_ENABLE_UI2_GOLDEN_TESTS
  "Register UI2 golden snapshot tests (requires Node.js)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/etl/include)

add_executable(picoTracker_tests
  test_main.cpp
  foundation_types_tests.cpp
  fourcc_persistency_tests.cpp
  wavheader_tests.cpp
  wasm_audio_tests.cpp
  wasm_application_snapshot_tests.cpp
  wasm_midi_tests.cpp
  wasm_logging_tests.cpp
  wasm_trace_tests.cpp
  wasm_platform_tests.cpp
  node_path_resolver_tests.cpp
  project_defaults_tests.cpp
  sample_pool_loading_tests.cpp
  ui2_browser_legacy_snapshot_tests.cpp
  ui2_dialog_adapter_tests.cpp
  ui2_project_controller_tests.cpp
  ui2_project_lifecycle_controller_tests.cpp
  ui2_device_lifecycle_controller_tests.cpp
  ui2_rename_render_tests.cpp
  ui2_legacy_snapshot_tests.cpp
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
  ../sources/Application/Instruments/I_Instrument.cpp
  ../sources/Application/UI2/Ui2SampleWaveformBackend.cpp
  ../sources/Application/Utils/HexBuffers.cpp
  ../sources/Application/Utils/char.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Application/Persistency/PersistencyService.cpp
  ../sources/Application/Persistency/Persistent.cpp
  ../sources/Application/Persistency/InstrumentFileValidator.cpp
  ../sources/Application/Model/Table.cpp
  ../sources/Application/Model/Chain.cpp
  ../sources/Application/Model/Groove.cpp
  ../sources/Application/Model/Phrase.cpp
  ../sources/Application/Model/ProjectParameterRestore.cpp
  ../sources/Application/Model/Song.cpp
  ../sources/Foundation/Services/Service.cpp
  ../sources/Foundation/Services/ServiceRegistry.cpp
  ../sources/Foundation/Services/SubService.cpp
  ../sources/Foundation/Variables/Variable.cpp
  ../sources/Foundation/Variables/VariableContainer.cpp
  ../sources/System/io/Status.cpp
  ../sources/Externals/TinyXML2/tinyxml2_printer_only.cpp
  ../sources/Externals/TinyXML2/tinyxml2adapter.cpp
  ../sources/Externals/yxml/yxml.c
  nanoprintf_test.cpp
  ../sources/Adapters/wasm/system/WasmSystem.cpp
  ../sources/Adapters/wasm/timer/WasmTimer.cpp
  ../sources/Adapters/wasm/filesystem/WasmFile.cpp
  ../sources/Adapters/wasm/filesystem/WasmFileSystem.cpp
  ../sources/Adapters/wasm/filesystem/WasmStorageBridge.cpp
  ../sources/Adapters/node/filesystem/PathResolver.cpp
  ../sources/Application/Instruments/SamplePoolLoading.cpp
  ../sources/Adapters/wasm/gui/WasmUiPresenter.cpp
  ../sources/Application/UI2/Ui2ApplicationRuntime.cpp
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
  ../sources/UI2/Views/Browser/UiBrowserView.cpp
  ../sources/UI2/Views/Chain/UiChainView.cpp
  ../sources/UI2/Views/Device/UiDeviceView.cpp
  ../sources/UI2/Views/Dialog/UiDialogView.cpp
  ../sources/UI2/Views/Font/UiFontView.cpp
  ../sources/UI2/Views/Groove/UiGrooveView.cpp
  ../sources/UI2/Views/Instrument/UiInstrumentView.cpp
  ../sources/UI2/Views/Mixer/UiMixerView.cpp
  ../sources/UI2/Views/Phrase/UiPhraseView.cpp
  ../sources/UI2/Views/Project/UiProjectView.cpp
  ../sources/UI2/Views/Record/UiRecordView.cpp
  ../sources/UI2/Views/Sample/UiSampleViews.cpp
  ../sources/UI2/Views/Song/UiSongView.cpp
  ../sources/UI2/Views/Table/UiTableView.cpp
  ../sources/UI2/Views/Theme/UiThemeView.cpp
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
add_test(NAME picoTracker_unit COMMAND picoTracker_tests)

add_executable(autosave_coordinator_tests
  test_main.cpp
  autosave_coordinator_tests.cpp
  ../sources/Application/Session/AutoSaveCoordinator.cpp
)

target_include_directories(autosave_coordinator_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(autosave_coordinator_tests PRIVATE HOST_TEST)
add_test(NAME autosave_coordinator COMMAND autosave_coordinator_tests)

# Project payload journaling is deliberately independent of Service/XML/model
# state. Keep its power-loss protocol in a focused allocation-free host test.
add_executable(project_file_journal_tests
  test_main.cpp
  project_file_journal_tests.cpp
)

target_include_directories(project_file_journal_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(project_file_journal_tests PRIVATE HOST_TEST)
add_test(NAME project_file_journal COMMAND project_file_journal_tests)

add_executable(ui2_status_bridge_tests
  test_main.cpp
  nanoprintf_test.cpp
  ui2_status_bridge_tests.cpp
  ../sources/Application/UI2/Ui2StatusBridge.cpp
  ../sources/System/io/Status.cpp
)

target_include_directories(ui2_status_bridge_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(ui2_status_bridge_tests PRIVATE HOST_TEST)
add_test(NAME ui2_status_bridge COMMAND ui2_status_bridge_tests)

# Exercise the production grid model adapter against deterministic fixed-size
# Song/Chain/Phrase/Table storage without pulling the legacy application graph
# into this focused host test.
add_executable(ui2_tracker_session_model_port_tests
  test_main.cpp
  ui2_tracker_session_model_port_tests.cpp
  ../sources/Application/Model/Scale.cpp
  ../sources/Application/UI2/Ui2TrackerSessionModelPort.cpp
)

target_include_directories(ui2_tracker_session_model_port_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/grid_model_stubs
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(ui2_tracker_session_model_port_tests PRIVATE
  HOST_TEST
)
add_test(NAME ui2_tracker_session_model_port
         COMMAND ui2_tracker_session_model_port_tests)

# The mailbox is deliberately FreeRTOS-free. Keep its ordering/repeat/release
# invariants in a focused host binary so Node adapter headers do not leak into
# the already large cross-platform application test target.
add_executable(node_ui2_input_mailbox_tests
  test_main.cpp
  node_ui2_input_mailbox_tests.cpp
  ../sources/Adapters/node/ui2/NodeUi2InputMailbox.cpp
)

target_include_directories(node_ui2_input_mailbox_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(node_ui2_input_mailbox_tests PRIVATE HOST_TEST)
add_test(NAME node_ui2_input_mailbox COMMAND node_ui2_input_mailbox_tests)

# The product UI2-only gate remains opt-in, but its artifact parser is a normal
# host test so forbidden/required contract changes cannot silently weaken it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME ui2_only_firmware_acceptance_checker
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_LIST_DIR}/../tools/tests"
      -p "test_verify_ui2_only_firmware.py")
endif()

# UI2-native controller tests are registered append-only to avoid churn in the
# legacy-heavy source list above while controller extraction is in progress.
target_sources(picoTracker_tests PRIVATE
  ui2_project_browser_controller_tests.cpp
  ui2_sample_browser_controller_tests.cpp
  ui2_sample_editor_controller_tests.cpp
  ui2_settings_controller_tests.cpp
  ui2_tracker_grid_controller_tests.cpp
  ui2_tracker_controller_hub_tests.cpp
  ui2_instrument_import_transaction_tests.cpp
  ui2_instrument_workflow_tests.cpp
  instrument_export_transaction_tests.cpp
  ui2_instrument_parameter_tests.cpp
  ui2_project_render_controller_tests.cpp
  firmware_lifecycle_tests.cpp
  ../sources/Application/Session/FirmwareLifecycleController.cpp
  ../sources/Application/Session/FirmwareLifecycleService.cpp
)

# Config has a lightweight host stub used by several unrelated controller
# tests. Keep the production Config theme transaction tests in a focused binary
# so the two intentionally different class definitions never share a process.
add_executable(theme_transaction_tests
  test_main.cpp
  nanoprintf_test.cpp
  theme_transaction_tests.cpp
  ../sources/Application/Model/Config.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Foundation/Observable.cpp
  ../sources/Foundation/Variables/Variable.cpp
  ../sources/Foundation/Variables/VariableContainer.cpp
  ../sources/Foundation/Variables/WatchedVariable.cpp
  ../sources/Externals/TinyXML2/tinyxml2_printer_only.cpp
  ../sources/Externals/TinyXML2/tinyxml2adapter.cpp
  ../sources/Externals/yxml/yxml.c
)

target_include_directories(theme_transaction_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

target_compile_definitions(theme_transaction_tests PRIVATE HOST_TEST)
add_test(NAME theme_transactions COMMAND theme_transaction_tests)

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

add_executable(ui2_topbar_snapshot_tool
  ui2_topbar_snapshot_main.cpp
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

target_include_directories(ui2_topbar_snapshot_tool PRIVATE
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

add_executable(ui2_instrument_snapshot_tool
  ui2_instrument_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Instrument/UiInstrumentView.cpp)

target_include_directories(ui2_instrument_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_mixer_snapshot_tool
  ui2_mixer_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Render/UiVuGradient.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Mixer/UiMixerView.cpp)

target_include_directories(ui2_mixer_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_groove_snapshot_tool
  ui2_groove_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Groove/UiGrooveView.cpp)

target_include_directories(ui2_groove_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_chain_snapshot_tool
  ui2_chain_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Render/UiVuGradient.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Chain/UiChainView.cpp)

target_include_directories(ui2_chain_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_project_snapshot_tool
  ui2_project_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Project/UiProjectView.cpp)

target_include_directories(ui2_project_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_device_snapshot_tool
  ui2_device_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Device/UiDeviceView.cpp)

target_include_directories(ui2_device_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_settings_snapshot_tool
  ui2_settings_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Font/UiFontView.cpp
  ../sources/UI2/Views/Theme/UiThemeView.cpp)

target_include_directories(ui2_settings_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_browser_snapshot_tool
  ui2_browser_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Browser/UiBrowserView.cpp)

target_include_directories(ui2_browser_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_record_snapshot_tool
  ui2_record_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Record/UiRecordView.cpp)

target_include_directories(ui2_record_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_sample_snapshot_tool
  ui2_sample_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Sample/UiSampleViews.cpp)

target_include_directories(ui2_sample_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

add_executable(ui2_dialog_snapshot_tool
  ui2_dialog_snapshot_main.cpp
  ../sources/UI2/Chrome/UiBarResolver.cpp
  ../sources/UI2/Chrome/UiChromeRenderer.cpp
  ../sources/UI2/Render/UiDirtyTiles.cpp
  ../sources/UI2/Render/UiFrameRenderer.cpp
  ../sources/UI2/Render/UiIndexedSurface.cpp
  ../sources/UI2/Render/UiRasterizer.cpp
  ../sources/UI2/Render/UiVuGradient.cpp
  ../sources/UI2/Text/UiFont5x7.cpp
  ../sources/UI2/Theme/UiPalette.cpp
  ../sources/UI2/Views/Dialog/UiDialogView.cpp
  ../sources/UI2/Views/Song/UiSongView.cpp)

target_include_directories(ui2_dialog_snapshot_tool PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../sources)

if(PICOTRACKER_ENABLE_UI2_GOLDEN_TESTS)
  find_program(NODE_EXECUTABLE node)
  if(NOT NODE_EXECUTABLE)
    message(FATAL_ERROR
      "PICOTRACKER_ENABLE_UI2_GOLDEN_TESTS requires Node.js")
  endif()

  add_test(NAME ui2_song_snapshot
    COMMAND ui2_snapshot_tool "${CMAKE_CURRENT_BINARY_DIR}/song-actual.ppm")
  add_test(NAME ui2_song_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/song-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/song.png"
      --tolerance-region 219,47,16,153,2)
  set_tests_properties(ui2_song_golden PROPERTIES DEPENDS ui2_song_snapshot)

  foreach(TOPBAR_STATE playing low high charging nav)
    add_test(NAME ui2_topbar_${TOPBAR_STATE}_snapshot
      COMMAND ui2_topbar_snapshot_tool "${TOPBAR_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/topbar-${TOPBAR_STATE}-actual.ppm")
    add_test(NAME ui2_topbar_${TOPBAR_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/topbar-${TOPBAR_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/topbar-${TOPBAR_STATE}.png"
        --tolerance-region 219,47,16,153,2)
    set_tests_properties(ui2_topbar_${TOPBAR_STATE}_golden PROPERTIES
      DEPENDS ui2_topbar_${TOPBAR_STATE}_snapshot)
  endforeach()

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

  foreach(INSTRUMENT_STATE none name number sample midi sid opal)
    add_test(NAME ui2_instrument_${INSTRUMENT_STATE}_snapshot
      COMMAND ui2_instrument_snapshot_tool "${INSTRUMENT_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/instrument-${INSTRUMENT_STATE}-actual.ppm")
    add_test(NAME ui2_instrument_${INSTRUMENT_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/instrument-${INSTRUMENT_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/instrument-${INSTRUMENT_STATE}.png")
    set_tests_properties(ui2_instrument_${INSTRUMENT_STATE}_golden PROPERTIES
      DEPENDS ui2_instrument_${INSTRUMENT_STATE}_snapshot)
  endforeach()

  add_test(NAME ui2_mixer_snapshot
    COMMAND ui2_mixer_snapshot_tool
      "${CMAKE_CURRENT_BINARY_DIR}/mixer-actual.ppm")
  add_test(NAME ui2_mixer_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/mixer-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/mixer.png"
      --tolerance-region 7,46,219,153,2)
  set_tests_properties(ui2_mixer_golden PROPERTIES
    DEPENDS ui2_mixer_snapshot)

  add_test(NAME ui2_groove_snapshot
    COMMAND ui2_groove_snapshot_tool
      "${CMAKE_CURRENT_BINARY_DIR}/groove-actual.ppm")
  add_test(NAME ui2_groove_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/groove-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/groove.png")
  set_tests_properties(ui2_groove_golden PROPERTIES
    DEPENDS ui2_groove_snapshot)

  add_test(NAME ui2_chain_snapshot
    COMMAND ui2_chain_snapshot_tool
      "${CMAKE_CURRENT_BINARY_DIR}/chain-actual.ppm")
  add_test(NAME ui2_chain_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/chain-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/chain.png"
      --tolerance-region 219,47,16,153,2)
  set_tests_properties(ui2_chain_golden PROPERTIES
    DEPENDS ui2_chain_snapshot)

  foreach(PROJECT_STATE name playback cleanup render)
    set(PROJECT_GOLDEN "project-${PROJECT_STATE}")
    if(PROJECT_STATE STREQUAL "playback")
      set(PROJECT_GOLDEN "project")
    endif()
    add_test(NAME ui2_project_${PROJECT_STATE}_snapshot
      COMMAND ui2_project_snapshot_tool "${PROJECT_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/project-${PROJECT_STATE}-actual.ppm")
    add_test(NAME ui2_project_${PROJECT_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/project-${PROJECT_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/${PROJECT_GOLDEN}.png")
    set_tests_properties(ui2_project_${PROJECT_STATE}_golden PROPERTIES
      DEPENDS ui2_project_${PROJECT_STATE}_snapshot)
  endforeach()

  add_test(NAME ui2_device_snapshot
    COMMAND ui2_device_snapshot_tool
      "${CMAKE_CURRENT_BINARY_DIR}/device-actual.ppm")
  add_test(NAME ui2_device_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/device-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/device.png")
  set_tests_properties(ui2_device_golden PROPERTIES
    DEPENDS ui2_device_snapshot)

  foreach(SETTINGS_STATE theme theme-scroll font)
    add_test(NAME ui2_${SETTINGS_STATE}_snapshot
      COMMAND ui2_settings_snapshot_tool "${SETTINGS_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${SETTINGS_STATE}-actual.ppm")
    add_test(NAME ui2_${SETTINGS_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/${SETTINGS_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/${SETTINGS_STATE}.png")
    set_tests_properties(ui2_${SETTINGS_STATE}_golden PROPERTIES
      DEPENDS ui2_${SETTINGS_STATE}_snapshot)
  endforeach()

  foreach(BROWSER_STATE sample-pool instrument-import projects theme-import)
    add_test(NAME ui2_browser_${BROWSER_STATE}_snapshot
      COMMAND ui2_browser_snapshot_tool "${BROWSER_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${BROWSER_STATE}-actual.ppm")
    add_test(NAME ui2_browser_${BROWSER_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/${BROWSER_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/${BROWSER_STATE}.png")
    set_tests_properties(ui2_browser_${BROWSER_STATE}_golden PROPERTIES
      DEPENDS ui2_browser_${BROWSER_STATE}_snapshot)
  endforeach()

  add_test(NAME ui2_record_snapshot
    COMMAND ui2_record_snapshot_tool
      "${CMAKE_CURRENT_BINARY_DIR}/record-actual.ppm")
  add_test(NAME ui2_record_golden
    COMMAND "${NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
      --actual "${CMAKE_CURRENT_BINARY_DIR}/record-actual.ppm"
      --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/record.png")
  set_tests_properties(ui2_record_golden PROPERTIES
    DEPENDS ui2_record_snapshot)

  foreach(SAMPLE_STATE editor slices)
    add_test(NAME ui2_sample_${SAMPLE_STATE}_snapshot
      COMMAND ui2_sample_snapshot_tool "${SAMPLE_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/sample-${SAMPLE_STATE}-actual.ppm")
    add_test(NAME ui2_sample_${SAMPLE_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/sample-${SAMPLE_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/sample-${SAMPLE_STATE}.png")
    set_tests_properties(ui2_sample_${SAMPLE_STATE}_golden PROPERTIES
      DEPENDS ui2_sample_${SAMPLE_STATE}_snapshot)
  endforeach()

  foreach(DIALOG_STATE message input render full rename)
    add_test(NAME ui2_dialog_${DIALOG_STATE}_snapshot
      COMMAND ui2_dialog_snapshot_tool "${DIALOG_STATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/dialog-${DIALOG_STATE}-actual.ppm")
    add_test(NAME ui2_dialog_${DIALOG_STATE}_golden
      COMMAND "${NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../web/tools/compare-ui2-snapshot.mjs"
        --actual "${CMAKE_CURRENT_BINARY_DIR}/dialog-${DIALOG_STATE}-actual.ppm"
        --golden "${CMAKE_CURRENT_LIST_DIR}/../web/e2e/ui2-golden.spec.js-snapshots/dialog-${DIALOG_STATE}.png")
    set_tests_properties(ui2_dialog_${DIALOG_STATE}_golden PROPERTIES
      DEPENDS ui2_dialog_${DIALOG_STATE}_snapshot)
  endforeach()
endif()
