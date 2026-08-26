if(PICOTRACKER_UI STREQUAL "ui2")
  add_library(platform_system
    Ui2System.h Ui2System.cpp
    SamplePool.cpp
  )

  target_link_libraries(platform_system INTERFACE system_system
                                        PUBLIC platform_timer
                                        PUBLIC platform_audio
                                        PUBLIC node_hal
                                        PUBLIC services_audio
                                        PUBLIC services_midi
                                        PUBLIC platform_midi
                                        PUBLIC platform_filesystem
                                        PUBLIC platform
                                        PUBLIC etl
                                        PUBLIC idf::esp_adc
                                        PUBLIC idf::driver
                                        PUBLIC idf::esp_system
                                        PUBLIC idf::esp_timer)
else()
  add_library(platform_system
    System.h System.cpp
    EventQueue.h EventQueue.cpp
    input.h input.cpp
    SamplePool.cpp
  )

  target_link_libraries(platform_system INTERFACE system_system
                                        PUBLIC uiframework_simplebaseclasses
                                        PUBLIC platform_timer
                                        PUBLIC platform_audio
                                        PUBLIC node_hal
                                        PUBLIC services_audio
                                        PUBLIC platform_midi
                                        PUBLIC platform_gui
                                        PUBLIC platform_display
                                        PUBLIC platform_filesystem
                                        PUBLIC platform
                                        PUBLIC etl
                                        PUBLIC platform_usb
                                        PUBLIC idf::esp_adc
                                        PUBLIC idf::driver
                                        PUBLIC idf::esp_system
                                        PUBLIC idf::esp_timer)
endif()


target_include_directories(platform_system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
