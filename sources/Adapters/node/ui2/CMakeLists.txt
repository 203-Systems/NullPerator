add_library(platform_ui2
  NodeUi2InputMailbox.cpp
  NodeUi2Platform.cpp
)

target_include_directories(platform_ui2 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PICOTRACKER_ROOT}
)

target_link_libraries(platform_ui2 PUBLIC
  application_ui2
  ui2
  platform_rgb565_display
  platform_system
  platform_midi
  platform_usb
  platform
  node_hal
  idf::freertos
)
