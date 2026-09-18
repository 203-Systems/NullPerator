cmake_minimum_required(VERSION 3.20)
project(AndroidAdapterTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
enable_testing()
add_executable(android_audio_driver_test audio_driver_test.cpp
  ../../sources/Adapters/android/audio/AndroidAudioDriver.cpp
  ../../sources/Services/Audio/AudioDriver.cpp
  ../../sources/Foundation/Observable.cpp)
target_include_directories(android_audio_driver_test PRIVATE stubs ../../sources/config ../../sources ../../sources/Externals/etl/include ../../sources/Adapters/android/platform)
add_test(NAME android_audio_driver COMMAND android_audio_driver_test)
