add_subdirectory(Model)
add_subdirectory(Session)

option(PICOTRACKER_UI2_LEGACY_STATE_SOURCE
  "Build the AppWindow/View compatibility source for UiApplicationRuntime"
  ON)

add_library(application
  AppWindow.cpp
  Application.cpp
  UI2/Ui2ApplicationRuntime.cpp
  UI2/Ui2NativeApplicationStateSource.cpp
  UI2/Ui2TrackerApplication.cpp
  UI2/Ui2TrackerSessionModelPort.cpp
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
  ui2
)

target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

add_subdirectory(Instruments)
add_subdirectory(Player)
add_subdirectory(Mixer)
add_subdirectory(Utils)
add_subdirectory(Commands)
add_subdirectory(Persistency)
add_subdirectory(Views)
add_subdirectory(Audio)
