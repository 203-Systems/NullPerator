add_library(platform_audio
  Audio.h Audio.cpp
  AudioDriver.h AudioDriver.cpp
  record.cpp
)

target_link_libraries(platform_audio PUBLIC
  platform_utils
  platform
  idf::driver
  idf::freertos
)

target_include_directories(platform_audio PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

include_directories(${PROJECT_SOURCE_DIR})
