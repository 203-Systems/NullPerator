add_subdirectory(Model)
add_subdirectory(Session)

option(PICOTRACKER_UI2_LEGACY_STATE_SOURCE
  "Build the AppWindow/View compatibility source for UiApplicationRuntime"
  ON)

set(PICOTRACKER_APPLICATION_UI2_SOURCES
  UI2/Ui2ApplicationRuntime.cpp
  UI2/Ui2NativeApplicationStateSource.cpp
  UI2/Ui2ProjectRenderBackend.cpp
  UI2/Ui2SampleFileOperations.cpp
  UI2/Ui2SampleWaveformBackend.cpp
  UI2/Ui2StatusBridge.cpp
  UI2/Ui2TrackerApplication.cpp
  UI2/Ui2TrackerSessionModelPort.cpp
)

set(PICOTRACKER_BUILD_LEGACY_APPLICATION ON)
if(DEFINED PICOTRACKER_UI AND PICOTRACKER_UI STREQUAL "ui2")
  set(PICOTRACKER_BUILD_LEGACY_APPLICATION OFF)
endif()

if(PICOTRACKER_BUILD_LEGACY_APPLICATION)
  add_library(application
    AppWindow.cpp
    Application.cpp
    ${PICOTRACKER_APPLICATION_UI2_SOURCES}
  )

  if(PICOTRACKER_UI2_LEGACY_STATE_SOURCE)
    target_sources(application PRIVATE
      UI2/Ui2LegacyApplicationStateSource.cpp
    )
  endif()

  target_link_libraries(application PUBLIC
    uiframework_simplebaseclasses
    application_mixer
    system_process
    application_commands
    services_midi
    application_instruments
    application_model
    application_persistency
    application_player
    application_lifecycle
    application_session
    application_views_baseclasses
    application_views
    application_views_modaldialogs
    uiframework_basicdatas
    foundation_variables
    system_io
    ui2
  )

  target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Native UI2 application boundary for product adapters that no longer compile
# AppWindow, Application, Ui2LegacyApplicationStateSource, or legacy View
# implementations. Keep this as a separate archive while the current products
# still link `application`; selecting it is an explicit adapter cutover.
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
if(PICOTRACKER_BUILD_LEGACY_APPLICATION)
  add_subdirectory(Views)
endif()
