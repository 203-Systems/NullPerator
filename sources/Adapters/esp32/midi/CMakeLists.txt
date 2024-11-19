add_library(platform_midi
  picoTrackerMidiService.h picoTrackerMidiService.cpp
  picoTrackerMidiDevice.h picoTrackerMidiDevice.cpp
  picoTrackerUSBMidiDevice.h picoTrackerUSBMidiDevice.cpp
)

target_link_libraries(platform_midi PRIVATE platform_usb PUBLIC idf::driver
)

target_include_directories(platform_midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

