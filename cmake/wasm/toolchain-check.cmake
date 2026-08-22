if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "The WASM target requires the Emscripten emcmake/em++ toolchain")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "3.1.54")
  message(FATAL_ERROR "PicoTracker WASM requires Emscripten 3.1.54 or newer")
endif()
