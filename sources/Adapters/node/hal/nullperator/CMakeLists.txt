idf_component_register(
    SRCS
        "nullperatorHAL.cpp"
        "system/system.cpp"
        "power/power.cpp"
        "input/input.cpp"
        "audio/audio.cpp"
        "audio/codec/es8389.cpp"
        "display/display.cpp"
        "imu/imu.cpp"
        "storage/storage.cpp"
        "midi/midi.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_driver_i2s
        esp_driver_ledc
        esp_adc
        driver
        esp_lcd
        fatfs
        esp_io_expander
        esp_codec_dev
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
