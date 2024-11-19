set(TINYUSB "${CMAKE_SOURCE_DIR}/Externals/TinyUsb")

add_library(platform_usb
  usb_utils.h
  usb_utils.cpp
  usb_descriptors.c
  # common
  ${TINYUSB}/src/tusb.c
  ${TINYUSB}/src/common/tusb_fifo.c
  # device
  ${TINYUSB}/src/device/usbd.c
  ${TINYUSB}/src/device/usbd_control.c
  ${TINYUSB}/src/class/audio/audio_device.c
  ${TINYUSB}/src/class/cdc/cdc_device.c
  ${TINYUSB}/src/class/dfu/dfu_device.c
  ${TINYUSB}/src/class/dfu/dfu_rt_device.c
  ${TINYUSB}/src/class/hid/hid_device.c
  ${TINYUSB}/src/class/midi/midi_device.c
  ${TINYUSB}/src/class/msc/msc_device.c
  ${TINYUSB}/src/class/net/ecm_rndis_device.c
  ${TINYUSB}/src/class/net/ncm_device.c
  ${TINYUSB}/src/class/usbtmc/usbtmc_device.c
  ${TINYUSB}/src/class/vendor/vendor_device.c
  ${TINYUSB}/src/class/video/video_device.c
  ${TINYUSB}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB}/src/portable/synopsys/dwc2/hcd_dwc2.c
  ${TINYUSB}/src/portable/synopsys/dwc2/dwc2_common.c
  # host
)

target_link_libraries(platform_usb
  PUBLIC idf::freertos
)
target_include_directories(platform_usb
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} # Current directory
    "${TINYUSB}/hw"            # TinyUSB hardware headers
    "${TINYUSB}/src"           # TinyUSB source headers
)

target_compile_options(platform_usb PUBLIC
  "-DCFG_TUSB_MCU=OPT_MCU_ESP32S3"
  "-DCFG_TUSB_OS=OPT_OS_FREERTOS"
)
  
include_directories(${PROJECT_SOURCE_DIR})