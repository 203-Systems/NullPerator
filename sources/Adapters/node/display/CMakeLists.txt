add_library(platform_display
  font.h
  display.h display.c
#  mode1.h mode1.c
#  mode2.h mode1.c
)

set_source_files_properties(display.c PROPERTIES LANGUAGE CXX)

target_link_libraries(platform_display PUBLIC idf::driver idf::freertos node_hal)

target_include_directories(platform_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
