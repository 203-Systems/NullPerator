add_library(system_process
  SysMutex.h SysMutex.cpp
)

target_link_libraries(system_process PUBLIC idf::freertos
)

target_include_directories(system_process PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
