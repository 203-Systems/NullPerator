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

target_link_libraries(${COMPONENT_LIB} PUBLIC
    # Strict UI2 platform graph.
    platform_ui2
    platform_system
    platform_audio
    platform_timer
    platform_mutex
    platform_rgb565_display
    platform_usb
    platform_midi
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
    system_console
    system_process
    system_io
    system_system
    system_filesystem
    foundation_services
    foundation_variables
    ui2)

target_include_directories(${COMPONENT_LIB} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${PICOTRACKER_ROOT}"
  "${PICOTRACKER_ROOT}/Adapters/node")
