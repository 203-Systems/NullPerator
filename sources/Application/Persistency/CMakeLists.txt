include("${PROJECT_SOURCE_DIR}/../cmake/CoreSourceGroups.cmake")
add_library(application_persistency
  InstrumentFileValidator.cpp
  Persistent.cpp
  ${PICOTRACKER_PERSISTENCY_SERVICE_SOURCES}
  PersistencyDocument.cpp
)

target_link_libraries(application_persistency PUBLIC
  foundation_services
  yxml
  tinyxml2
  etl
  application_model
)

target_include_directories(application_persistency PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
