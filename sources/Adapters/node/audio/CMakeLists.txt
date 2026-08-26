add_library(platform_audio
  Audio.h Audio.cpp
  AudioDriver.h AudioDriver.cpp
)

target_link_libraries(platform_audio PUBLIC
  platform
  idf::driver
  idf::freertos
)

target_include_directories(platform_audio PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Recording is an application-facing adapter facade, not part of the Audio
# service object's implementation. Keeping it in a separate archive expresses
# its one-way link order accurately: platform_system owns NodeAudio, while the
# later application_ui2 archive calls these recording entry points.
add_library(platform_recording
  record.h record.cpp
)

target_link_libraries(platform_recording PUBLIC
  system_console
)

target_include_directories(platform_recording PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

include_directories(${PROJECT_SOURCE_DIR})
