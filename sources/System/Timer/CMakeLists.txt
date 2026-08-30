add_library(system_timer
  Timer.cpp
)

target_link_libraries(system_timer PUBLIC
  foundation
)

target_include_directories(system_timer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
