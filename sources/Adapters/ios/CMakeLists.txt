cmake_minimum_required(VERSION 3.20)

include_directories(
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")

# Product geometry is shared by UI2; only the iOS capability is selected here.
add_compile_definitions(NULLPERATOR_IOS)
add_compile_options(
  -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/IOSMemorySections.h")

add_library(platform
  platform/IOSPlatform.cpp)
target_include_directories(platform PUBLIC
  "${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")

add_library(platform_timer timer/IOSTimer.cpp)
target_include_directories(platform_timer PUBLIC "${PROJECT_SOURCE_DIR}")

# Shared host adapters contain no Web API exports or target SDK branches.
add_library(platform_tracing
  "${PROJECT_SOURCE_DIR}/System/Console/Profiler.cpp")
target_include_directories(platform_tracing PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_filesystem
  "${PROJECT_SOURCE_DIR}/Adapters/posix/filesystem/PosixFile.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/posix/filesystem/PosixFileSystem.cpp"
)
target_include_directories(platform_filesystem PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_filesystem PUBLIC platform_tracing)

# CoreMIDI crosses the Swift boundary, while the firmware-facing byte parser
# and bounded output queue stay in the native C++ runtime.
add_library(platform_midi
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/MidiLatencyTrace.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/QueuedMidiInDevice.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/QueuedMidiOutDevice.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/QueuedMidiService.cpp")
target_include_directories(platform_midi PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_midi PUBLIC platform_tracing)

add_library(platform_audio
  audio/IOSAudio.cpp
  audio/IOSAudioDriver.cpp
  audio/IOSRecordingPlatform.cpp)
target_include_directories(platform_audio PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_audio PUBLIC "-framework AudioToolbox")

add_subdirectory("${PROJECT_SOURCE_DIR}/UI2"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/UI2")
add_subdirectory("${PROJECT_SOURCE_DIR}/System"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/System")
add_subdirectory("${PROJECT_SOURCE_DIR}/Foundation"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Foundation")
add_subdirectory("${PROJECT_SOURCE_DIR}/Externals"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Externals")
add_subdirectory("${PROJECT_SOURCE_DIR}/Services"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Services")
add_subdirectory("${PROJECT_SOURCE_DIR}/Application"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Application")

add_library(nullperator_ios_core STATIC
  gui/IOSUiPresenter.cpp
  runtime/IOSNativeRuntime.cpp
  system/IOSSystem.cpp
  "${PROJECT_SOURCE_DIR}/Adapters/common/system/HeapSamplePool.cpp")
target_include_directories(nullperator_ios_core PUBLIC "${PROJECT_SOURCE_DIR}")
execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
  OUTPUT_VARIABLE NULLPERATOR_BUILD_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE NULLPERATOR_GIT_RESULT)
if(NOT NULLPERATOR_GIT_RESULT EQUAL 0 OR NULLPERATOR_BUILD_HASH STREQUAL "")
  set(NULLPERATOR_BUILD_HASH "unknown")
endif()
string(TIMESTAMP NULLPERATOR_BUILD_TIME "%Y-%m-%dT%H:%M:%SZ" UTC)
target_compile_definitions(nullperator_ios_core PRIVATE
  NULLPERATOR_BUILD_HASH="${NULLPERATOR_BUILD_HASH}"
  NULLPERATOR_BUILD_TIME="${NULLPERATOR_BUILD_TIME}")
target_link_libraries(nullperator_ios_core PUBLIC
  application_ui2
  services_audio
  platform_audio
  platform_filesystem
  platform_midi
  platform_timer
  platform
  system_timer
  foundation
  "-framework AudioToolbox")

add_library(platform_memory "${PROJECT_SOURCE_DIR}/Adapters/common/memory/Memory.cpp")
target_include_directories(platform_memory PUBLIC "${PROJECT_SOURCE_DIR}")

# Enumerate only targets in this configured native graph. Scanning the build
# directory for *.a can silently merge removed adapters or another configuration.
function(collect_ios_archives directory output)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  set(archives "")
  foreach(target IN LISTS targets)
    get_target_property(kind "${target}" TYPE)
    if(kind STREQUAL "STATIC_LIBRARY")
      string(APPEND archives "$<TARGET_FILE:${target}>\n")
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    collect_ios_archives("${child}" child_archives)
    string(APPEND archives "${child_archives}")
  endforeach()
  set(${output} "${archives}" PARENT_SCOPE)
endfunction()
collect_ios_archives("${CMAKE_CURRENT_SOURCE_DIR}" ios_archives)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/native-core-archives-$<CONFIG>.txt"
  CONTENT "${ios_archives}")
