add_library(platform_audio
  picoTrackerAudio.h picoTrackerAudio.cpp
  picoTrackerAudioDriver.h picoTrackerAudioDriver.cpp
)

target_link_libraries(platform_audio PUBLIC platform_utils)

target_include_directories(platform_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR})
