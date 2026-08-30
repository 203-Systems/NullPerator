add_library(opal
  opal.cpp
)

# This diagnostic is GCC-specific; passing it to Clang/Emscripten creates an
# unknown-option warning on every clean WASM build.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(opal PRIVATE -Wno-aggressive-loop-optimizations)
endif()

target_link_libraries(opal 
    PUBLIC application_utils
)

target_include_directories(opal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
