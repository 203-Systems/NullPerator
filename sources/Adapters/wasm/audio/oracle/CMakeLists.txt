# This fixture is entered before the parent applies its product pthread
# option. It validates the same pure resampler helper in a no-main module.

add_executable(picotracker_wasm_oracle
  ../AudioRenderOracle.cpp
  ../OutputResampler.cpp
  ../wasm_oracle_main.cpp)
target_include_directories(picotracker_wasm_oracle PRIVATE "${PROJECT_SOURCE_DIR}")
target_compile_options(picotracker_wasm_oracle PRIVATE
  -Wno-error
  -Wno-deprecated-declarations
  -Wno-unused-parameter)
target_link_options(picotracker_wasm_oracle PRIVATE
  --no-entry
  -sSTANDALONE_WASM=1
  "-sEXPORTED_FUNCTIONS=['_PicoTracker_Wasm_GetRenderOracle']")
set_target_properties(picotracker_wasm_oracle PROPERTIES
  OUTPUT_NAME picotracker-oracle
  SUFFIX ".wasm"
  RUNTIME_OUTPUT_DIRECTORY "${PICOTRACKER_WEB_WASM_DIR}")
