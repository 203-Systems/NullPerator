add_library(application_mixer
  MixerService.cpp
)

target_link_libraries(application_mixer PUBLIC
  application_audio
  foundation
  system_console
  platform
)

target_include_directories(application_mixer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
