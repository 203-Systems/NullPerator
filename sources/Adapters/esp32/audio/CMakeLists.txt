add_library(platform_audio
  picoTrackerAudio.h picoTrackerAudio.cpp
  picoTrackerAudioDriver.h picoTrackerAudioDriver.cpp
  ES8388.h ES8388.cpp
)

target_link_libraries(platform_audio PUBLIC platform_utils idf::driver idf::freertos)

target_include_directories(platform_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
