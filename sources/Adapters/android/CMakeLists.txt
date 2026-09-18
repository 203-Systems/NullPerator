cmake_minimum_required(VERSION 3.20)

include_directories(
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")

# Android uses the shared 240 x 240 UI2 geometry.
add_compile_definitions(NULLPERATOR_ANDROID)
add_compile_options(
  -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/AndroidMemorySections.h")

add_library(platform
  platform/AndroidPlatform.cpp)
target_include_directories(platform PUBLIC
  "${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")

add_library(platform_timer timer/AndroidTimer.cpp)
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

# Keep the shared bounded MIDI queue in the core graph.
add_library(platform_midi
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/MidiLatencyTrace.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/QueuedMidiInDevice.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/QueuedMidiOutDevice.cpp"
  "${PROJECT_SOURCE_DIR}/Adapters/common/midi/QueuedMidiService.cpp")
target_include_directories(platform_midi PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_midi PUBLIC platform_tracing)

add_library(platform_audio
  audio/AndroidAudio.cpp
  audio/AndroidAudioDriver.cpp
  audio/AndroidRecordingPlatform.cpp)
target_include_directories(platform_audio PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_audio PUBLIC aaudio log)

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

add_library(nullperator_android_core SHARED
  runtime/AndroidJni.cpp
  gui/AndroidUiPresenter.cpp
  runtime/AndroidNativeRuntime.cpp
  system/AndroidSystem.cpp
  "${PROJECT_SOURCE_DIR}/Adapters/common/system/HeapSamplePool.cpp")
target_include_directories(nullperator_android_core PUBLIC "${PROJECT_SOURCE_DIR}")
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
target_compile_definitions(nullperator_android_core PRIVATE
  NULLPERATOR_BUILD_HASH="${NULLPERATOR_BUILD_HASH}"
  NULLPERATOR_BUILD_TIME="${NULLPERATOR_BUILD_TIME}")
target_link_libraries(nullperator_android_core PUBLIC
  application_ui2
  services_audio
  platform_audio
  platform_filesystem
  platform_midi
  platform_timer
  platform
  system_timer
  foundation
  aaudio log)

add_library(platform_memory "${PROJECT_SOURCE_DIR}/Adapters/common/memory/Memory.cpp")
target_include_directories(platform_memory PUBLIC "${PROJECT_SOURCE_DIR}")

