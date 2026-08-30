add_library(application_player
  Player.cpp
  PlayerChannel.cpp
  PlayerMixer.cpp
  SyncMaster.cpp
  TablePlayback.cpp
)

target_link_libraries(application_player PUBLIC
  foundation
  application_mixer
  application_audio
  application_model
  application_session
)

target_include_directories(application_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
