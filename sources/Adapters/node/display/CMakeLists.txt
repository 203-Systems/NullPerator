add_library(platform_rgb565_display
  Rgb565DisplayTransport.cpp
)

target_link_libraries(platform_rgb565_display PUBLIC
  idf::driver
  idf::freertos
  node_hal
)

target_include_directories(platform_rgb565_display PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

include_directories(${PROJECT_SOURCE_DIR})
