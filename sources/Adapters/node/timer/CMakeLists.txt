add_library(platform_timer
  Timer.h Timer.cpp
)

target_link_libraries(platform_timer 
    PUBLIC idf::esp_timer
)

target_include_directories(platform_timer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${IDF_PATH}/components/esp_timer/include)

include_directories(
  ${PROJECT_SOURCE_DIR}
)
