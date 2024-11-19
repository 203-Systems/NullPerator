idf_component_register(SRCS picoTrackerMain.cpp
                    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}"
)

# add_executable(picoTracker picoTrackerMain.cpp)

target_link_libraries(${COMPONENT_LIB}
              PUBLIC platform_system
              PUBLIC platform
              PUBLIC platform_usb
              PUBLIC idf::freertos
)

target_include_directories(${COMPONENT_LIB}  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

# idf_build_executable(picoTracker)
