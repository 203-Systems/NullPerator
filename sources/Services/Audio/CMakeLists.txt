add_library(services_audio
  Audio.cpp
  AudioDriver.cpp
  AudioMixer.cpp
  AudioOut.cpp
  AudioOutDriver.cpp
)

target_link_libraries(services_audio PUBLIC
  services_wav
  foundation
)

target_include_directories(services_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

add_library(services_wav WavHeader.cpp WavFileWriter.cpp)
target_include_directories(services_wav PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(services_wav PUBLIC foundation system_console)
