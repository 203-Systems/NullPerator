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

add_library(platform_display
  font.h
  display.h display.c
#  mode1.h mode1.c
#  mode2.h mode1.c
)

set_source_files_properties(display.c PROPERTIES LANGUAGE CXX)

target_link_libraries(platform_display PUBLIC
  platform_rgb565_display
  idf::driver
  idf::freertos
  node_hal
)

target_include_directories(platform_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
