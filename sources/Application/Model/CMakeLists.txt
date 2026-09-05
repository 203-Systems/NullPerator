include("${PROJECT_SOURCE_DIR}/../cmake/CoreSourceGroups.cmake")
add_library(application_model
  Chain.cpp
  ${PICOTRACKER_CONFIG_SOURCES}
  Groove.cpp
  Mixer.cpp
  Phrase.cpp
  Project.cpp
  ProjectParameterRestore.cpp
  Scale.cpp
  Song.cpp
  Table.cpp
)

target_link_libraries(application_model PUBLIC
  foundation
  system_system
  foundation_variables
  application_utils
)

target_include_directories(application_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
