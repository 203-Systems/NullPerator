add_library(platform_filesystem
  FileSystem.cpp
)

target_link_libraries(platform_filesystem PUBLIC
  system_errors
  idf::fatfs
  idf::sdmmc
  idf::vfs
  idf::esp_timer
)

target_include_directories(platform_filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
