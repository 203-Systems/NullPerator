add_library(application_session
  TrackerSessionState.cpp
)

target_link_libraries(application_session PUBLIC
  application_model
)

target_include_directories(application_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(application_lifecycle
  TrackerApplicationSession.cpp
)

target_link_libraries(application_lifecycle PUBLIC
  application_commands
  application_instruments
  application_model
  application_persistency
  application_player
  application_session
)

target_include_directories(application_lifecycle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
