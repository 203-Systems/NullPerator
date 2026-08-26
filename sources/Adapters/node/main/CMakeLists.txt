idf_component_register(
  SRCS Main.cpp
  INCLUDE_DIRS
    "."
    "${CMAKE_SOURCE_DIR}"
    "${PICOTRACKER_ROOT}"
    "${PICOTRACKER_ROOT}/Adapters/node"
  LDFRAGMENTS
    opal_time_critical.lf
    dtcmram.lf
)

if(PICOTRACKER_UI STREQUAL "ui2")
  target_link_libraries(${COMPONENT_LIB} PUBLIC
    # Strict platform graph. No GUIFactory, EventManager, legacy display.c or
    # character Remote UI raster is present in this product configuration.
    platform_ui2
    platform_system
    platform_audio
    platform_timer
    platform_mutex
    platform_rgb565_display
    platform_usb
    platform_midi
    platform_utils
    platform_filesystem
    platform

    application_ui2
    application_lifecycle
    application_session
    application_mixer
    application_player
    application_commands
    application_persistency
    application_instruments
    application_model
    application_audio
    application_utils
    # Application/UI2 calls the recording facade; place its platform
    # implementation after the application archive in the one-pass link.
    platform_recording

    services_audio
    services_midi
    system_timer
    system_console
    system_process
    system_io
    system_system
    system_filesystem
    profiler
    foundation_services
    foundation_variables
    foundation
    ui2)
else()
  # Explicit legacy-reference product. This graph is retained only so old UI
  # behavior can be compared during migration; it is not the default firmware.
  target_link_libraries(${COMPONENT_LIB} PUBLIC
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
    application
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
    platform_recording
    services_audio
    services_midi
    system_timer
    system_console
    system_process
    system_io
    system_system
    profiler
    foundation_services
    foundation_variables
    foundation)
endif()

target_include_directories(${COMPONENT_LIB} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${PICOTRACKER_ROOT}"
  "${PICOTRACKER_ROOT}/Adapters/node")
