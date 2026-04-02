add_library(platform
  platform.h platform.cpp
  etl_profile.h
)

target_link_libraries(platform PUBLIC
  idf::driver
  idf::esp_timer
  node_hal
  platform_mutex
)

target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
