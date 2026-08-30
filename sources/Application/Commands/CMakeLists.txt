add_library(application_commands
  ApplicationCommandDispatcher.cpp
  EventDispatcher.cpp
)

target_link_libraries(application_commands
  PUBLIC
    application_model
    foundation
    system_timer
  PRIVATE
    application_player
)

target_include_directories(application_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
