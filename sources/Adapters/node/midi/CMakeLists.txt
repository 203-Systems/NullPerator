add_library(platform_midi
    MidiService.h MidiService.cpp
    MidiDevice.h MidiDevice.cpp
    MidiInDevice.h MidiInDevice.cpp
    USBMidiDevice.h USBMidiDevice.cpp
)

target_link_libraries(platform_midi PRIVATE platform_usb PUBLIC idf::driver
)

target_include_directories(platform_midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

