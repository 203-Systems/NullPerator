add_library(platform
  gpio.h
  platform.h platform.cpp
  etl_profile.h
  PCA95x5.h
)

target_link_libraries(platform PUBLIC idf::driver
)

target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
