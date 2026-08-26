cmake_minimum_required(VERSION 3.16)

if(NOT ESP_PLATFORM)
  message(FATAL_ERROR "ESP_PLATFORM is not set. Run from an ESP-IDF environment (idf.py --project-dir sources).")
endif()

# When this file is included from the top-level ESP-IDF project we only need to
# wire up the adapter targets. All global project configuration happens in
# sources/CMakeLists.txt.
if(NOT DEFINED PICOTRACKER_ROOT)
  get_filename_component(PICOTRACKER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(_expected_target "esp32s3")
if(NOT IDF_TARGET STREQUAL "${_expected_target}")
  message(FATAL_ERROR "Node adapter requires IDF_TARGET='${_expected_target}' (currently '${IDF_TARGET}').")
endif()

include_directories("${PICOTRACKER_ROOT}")
include_directories("${PICOTRACKER_ROOT}/Adapters/node")

add_subdirectory(hal)
add_subdirectory(system)
add_subdirectory(timer)
add_subdirectory(display)
add_subdirectory(filesystem)
add_subdirectory(audio)
add_subdirectory(utils)
add_subdirectory(midi)
add_subdirectory(platform)
add_subdirectory(usb)
add_subdirectory(mutex)
if(PICOTRACKER_UI STREQUAL "ui2")
  add_subdirectory(ui2)
else()
  add_subdirectory(gui)
endif()
