add_library(platform_mutex
  Mutex.cpp
)

target_link_libraries(platform_mutex PUBLIC idf::freertos
                                       PUBLIC system_console)

target_include_directories(platform_mutex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
