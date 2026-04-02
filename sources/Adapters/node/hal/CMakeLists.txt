add_library(node_hal
  nullperator/nullperatorHAL.cpp
  nullperator/system/io_expander.cpp
  nullperator/system/system.cpp
  nullperator/power/power.cpp
  nullperator/input/input.cpp
  nullperator/audio/audio.cpp
  nullperator/audio/codec/es8389.cpp
  nullperator/display/display.cpp
  nullperator/imu/imu.cpp
  nullperator/storage/storage.cpp
  nullperator/midi/midi.cpp
)

target_link_libraries(node_hal PUBLIC
  idf::driver
  idf::esp_driver_i2c
  idf::esp_adc
  idf::esp_lcd
  idf::fatfs
  idf::sdmmc
  idf::vfs
  idf::esp_timer
  idf::espressif__esp_codec_dev
)

target_include_directories(node_hal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/nullperator
  ${PICOTRACKER_ROOT}
)

target_compile_options(node_hal PRIVATE -Wno-missing-field-initializers)
