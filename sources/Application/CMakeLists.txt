add_subdirectory(Model)
add_subdirectory(Session)

set(PICOTRACKER_APPLICATION_UI2_SOURCES
  UI2/Ui2ApplicationRuntime.cpp
  UI2/Ui2NativeApplicationStateSource.cpp
  UI2/Ui2ProjectRenderBackend.cpp
  UI2/Ui2SampleFileOperations.cpp
  UI2/Ui2SampleEditorTransaction.cpp
  UI2/Ui2SampleWaveformBackend.cpp
  UI2/Ui2StatusBridge.cpp
  UI2/Ui2TrackerApplication.cpp
  UI2/Ui2TrackerSessionModelPort.cpp
)

# Native UI2 application boundary shared by Node and WASM.
add_library(application_ui2
  ${PICOTRACKER_APPLICATION_UI2_SOURCES}
)

target_link_libraries(application_ui2 PUBLIC
  application_lifecycle
  services_audio
  system_io
  ui2
)

target_include_directories(application_ui2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

add_subdirectory(Instruments)
add_subdirectory(Player)
add_subdirectory(Mixer)
add_subdirectory(Utils)
add_subdirectory(Commands)
add_subdirectory(Persistency)
add_subdirectory(Audio)
