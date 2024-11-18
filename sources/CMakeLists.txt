cmake_minimum_required(VERSION 3.16)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Adapters/esp32")

project(picoTracker)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Make this a picoTracker build within the project
add_definitions(-DPICOBUILD)

# For debugging purposes, print all mallocs
# add_definitions(-DPICO_DEBUG_MALLOC)
# add_definitions(-DPICOSTATS)
# add_definitions(-DALL_MALLOC)
# add_definitions(-DSHOW_MEM_USAGE)
# Perform a benchmark of SD card using SDIO oin startup. Can be removed once performance issues have been solved
# add_definitions(-DSDIO_BENCH)
# Enable loading samples into Flash
add_definitions(-DLOAD_IN_FLASH)
# Disable MIDI. Enable in order to use UART0 as stdio for debugging
# add_definitions(-DDUMMY_MIDI)
# Use USB for UI: keyboardinput & Display output. USB Uart MUST also be enabled
# below.
add_definitions(-DUSB_REMOTE_UI)
#disabled for now as USB **input** is not yet fully implemented
#add_definitions(USB_REMOTE_UI_INPUT)
# enable basic control REPL over serial port. NOTE: uses extra ~112bytes of *stack*
# add_definitions(-DSERIAL_REPL)


add_compile_options(
  -Werror
  -Wall
  -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
  -fstack-usage
  -fcallgraph-info=su # include above info in callgraph
  -Wstack-usage=2048 # allows high watermark at the time this option was added TODO: reduce this for safety and/or reduce stack size
  # TODO: SHOULD FIX
  -Wno-int-to-pointer-cast
  -Wno-error=return-type

  -Wno-overloaded-virtual
  -Wno-return-type
  -Wno-unused-function


  # braids
  -Wno-unused-variable
  -Wno-array-bounds
)

add_subdirectory(Adapters)
add_subdirectory(UIFramework)
add_subdirectory(System)
add_subdirectory(Application)
add_subdirectory(Externals)
add_subdirectory(Services)
add_subdirectory(Foundation)

set_property(TARGET picoTracker APPEND_STRING PROPERTY LINK_FLAGS "-Wl,--print-memory-usage")

# target_compile_definitions(picoTracker PUBLIC
# #	PICO_HEAP_SIZE=204800
#   PICO_DEBUG_MALLOC_LOW_WATER=1000
#   PICO_MALLOC_PANIC=0
#   PICO_STACK_SIZE=4096
#   PICO_CORE1_STACK_SIZE=4096 # Safe value. 2K seems to be enough. Is it for all use cases?
#   PICO_USE_STACK_GUARDS
#   PICO_FLASH_SIZE_BYTES=16*1024*1024
#   # workaround for slow init crystal on some boards see: https://github.com/raspberrypi/pico-sdk/pull/457
#   PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
# )