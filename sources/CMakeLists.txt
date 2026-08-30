cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

# PicoTracker headers historically include ETL through its vendored path.
# Newer ETL releases use <etl/...> for their own internal includes, so every
# product also needs the library's public include root.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Externals/etl/include")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json")

if(NOT DEFINED Node)
  set(Node false)
endif()

if(NOT DEFINED WASM)
  set(WASM false)
endif()

if(Node AND WASM)
  message(FATAL_ERROR "Node and WASM are mutually exclusive build targets")
endif()

# ESP-IDF node build (vanilla style). Invoke with -DNode=true from sources/.
if(Node)
  include("${CMAKE_CURRENT_SOURCE_DIR}/Adapters/node/esp-idf.cmake")
  adapter_node_setup()
  return()
endif()

# Browser build.
if(WASM)
  project(picoTrackerWasm LANGUAGES C CXX)
  include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/wasm/toolchain-check.cmake")
  add_subdirectory(Adapters/wasm)
  return()
endif()

message(FATAL_ERROR
  "No product selected. Configure the ESP32-S3 Node target with -DNode=true "
  "or the browser target with -DWASM=true.")
