cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

# PicoTracker headers historically include ETL through its vendored path.
# Newer ETL releases use <etl/...> for their own internal includes, so every
# product also needs the library's public include root.
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/Externals/etl/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/config")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json")

if(NOT DEFINED Node)
  set(Node false)
endif()

if(NOT DEFINED WASM)
  set(WASM false)
endif()

if(NOT DEFINED IOS)
  set(IOS false)
endif()

if(NOT DEFINED Android)
  set(Android false)
endif()

set(NULLPERATOR_PRODUCT_COUNT 0)
foreach(NULLPERATOR_PRODUCT Node WASM IOS Android)
  if(${NULLPERATOR_PRODUCT})
    math(EXPR NULLPERATOR_PRODUCT_COUNT "${NULLPERATOR_PRODUCT_COUNT} + 1")
  endif()
endforeach()
if(NULLPERATOR_PRODUCT_COUNT GREATER 1)
  message(FATAL_ERROR "Node, WASM, IOS and Android are mutually exclusive build targets")
endif()

if(Android)
  project(nullPeratorAndroid LANGUAGES C CXX)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(Adapters/android)
  return()
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

# Native iOS core. The Swift/Objective-C++ application remains an Xcode target;
# CMake only builds the portable tracker graph and its isolated iOS adapters.
if(IOS)
  project(nullPeratorIOSCore LANGUAGES C CXX)
  add_subdirectory(Adapters/ios)
  return()
endif()

message(FATAL_ERROR
  "No product selected. Configure the ESP32-S3 Node target with -DNode=true "
  "the browser target with -DWASM=true, the iOS core with -DIOS=true, "
  "or Android with -DAndroid=true and the NDK toolchain.")
