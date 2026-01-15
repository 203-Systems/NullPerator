idf_component_register(
  SRCS Main.cpp
  INCLUDE_DIRS
    "."
    "${CMAKE_SOURCE_DIR}"
    "${PICOTRACKER_ROOT}"
    "${PICOTRACKER_ROOT}/Adapters/node"
)

# Mirror the pico top-level link order so static libs resolve correctly.
target_link_libraries(${COMPONENT_LIB} PUBLIC
  # Platform adapters
  platform_system
  platform_audio
  platform_gui
  platform_timer
  platform_mutex
  platform_display
  platform_usb
  platform_midi
  platform_utils
  platform_filesystem
  platform

  # Application layer
  application
  application_controllers
  application_views
  application_views_baseclasses
  application_views_modaldialogs
  application_mixer
  application_player
  application_commands
  application_instruments
  application_model
  application_audio
  application_utils

  # Services
  services_audio
  services_midi
  services_controllers

  # System interfaces
  system_timer
  system_console
  system_errors
  system_process
  system_io
  system_system
  profiler

  # Foundation
  foundation_services
  foundation_variables
  foundation
)

target_include_directories(${COMPONENT_LIB} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${PICOTRACKER_ROOT}"
  "${PICOTRACKER_ROOT}/Adapters/node")
