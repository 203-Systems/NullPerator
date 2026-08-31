cmake_minimum_required(VERSION 3.13)

# Shared setup for the Node ESP-IDF build. Call pico_tracker_node_setup()
# from the project root (sources/CMakeLists.txt) when -DNode=true is set.
macro(adapter_node_setup)
  set(_node_root "${CMAKE_SOURCE_DIR}")
  set(_node_build_dir "${_node_root}/build/node")

  if(NOT "${CMAKE_BINARY_DIR}" STREQUAL "${_node_build_dir}")
    message(FATAL_ERROR "Configure Node builds in '${_node_build_dir}'. Re-run idf.py with '-B \"${_node_build_dir}\"'.")
  endif()

  set(PICOTRACKER_ROOT "${_node_root}" CACHE PATH "picoTracker root" FORCE)
  set(PICOTRACKER_IDF_BUILD_DIR "${_node_build_dir}" CACHE PATH "picoTracker ESP-IDF build dir" FORCE)
  set(SDKCONFIG "${_node_build_dir}/sdkconfig" CACHE FILEPATH "sdkconfig output for node" FORCE)
  if(EXISTS "${_node_root}/Adapters/node/sdkconfig.defaults")
    set(SDKCONFIG_DEFAULTS "${_node_root}/Adapters/node/sdkconfig.defaults" CACHE FILEPATH "sdkconfig.defaults for node" FORCE)
  endif()
  set(PARTITION_TABLE_FILENAME "${_node_root}/Adapters/node/partitions.csv" CACHE STRING "Partition table CSV" FORCE)
  set(PARTITION_TABLE_CSV "${_node_root}/Adapters/node/partitions.csv" CACHE STRING "Partition table CSV" FORCE)

  # Pull in ESP-IDF build helpers
  include($ENV{IDF_PATH}/tools/cmake/project.cmake)
  # Keep the component manager lock file alongside the adapter.
  idf_build_set_property(DEPENDENCIES_LOCK "${_node_root}/Adapters/node/dependencies.lock")

  # Use the Node adapter component plus its managed components (codec, etc).
  set(EXTRA_COMPONENT_DIRS
    "${_node_root}/Adapters/node/main"
    "${_node_root}/Adapters/node/managed_components/espressif__esp_codec_dev")
  message(STATUS "EXTRA_COMPONENT_DIRS: ${EXTRA_COMPONENT_DIRS}")

  # ESP-IDF discovers project libraries while project() is configuring. Seed
  # the vendored ETL version before that discovery starts so ETL does not run
  # its CMake-incompatible optional git-describe probe.
  file(STRINGS "${_node_root}/Externals/etl/version.txt"
       _picotracker_etl_version LIMIT_COUNT 1)
  set(ETL_VERSION "${_picotracker_etl_version}" CACHE STRING
      "Vendored ETL version" FORCE)
  set(GIT_DIR_LOOKUP_POLICY ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR)

  project(picoTracker)

  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  set(IDF_TARGET "esp32s3")

  add_definitions(-DNODE)
  add_definitions(-DESP_PLATFORM)

  # PicoTracker's shared libraries are regular CMake targets rather than
  # idf_component_register() components, so ESP-IDF does not propagate its
  # selected optimization level to them. Mirror the configured application
  # optimization here; otherwise the audio/render hot paths are built without
  # any -O level even when CONFIG_COMPILER_OPTIMIZATION_PERF is selected.
  if(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    add_compile_options(-Os)
  elseif(CONFIG_COMPILER_OPTIMIZATION_DEBUG)
    add_compile_options(-Og)
  elseif(CONFIG_COMPILER_OPTIMIZATION_NONE)
    add_compile_options(-O0)
  elseif(CONFIG_COMPILER_OPTIMIZATION_PERF)
    add_compile_options(-O2)
  endif()

  add_compile_options(
    -g
  )

  add_subdirectory(Adapters/node)
  add_subdirectory(UI2)
  add_subdirectory(System)
  add_subdirectory(Application)
  add_subdirectory(Externals)
  add_subdirectory(Services)
  add_subdirectory(Foundation)

  include("${_node_root}/../cmake/Ui2OnlyFirmwareAcceptance.cmake")
  pico_tracker_add_ui2_only_acceptance_check(
    NAME ui2_only_firmware_acceptance
    FIRMWARE_TARGET "${PROJECT_NAME}.elf"
    COMPILE_COMMANDS "${CMAKE_BINARY_DIR}/compile_commands.json"
    LINK_MAP "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map"
    ELF "$<TARGET_FILE:${PROJECT_NAME}.elf>")

  include("${_node_root}/../cmake/NodeIramBudget.cmake")
  pico_tracker_add_node_iram_budget_check(
    NAME node_iram_budget
    FIRMWARE_TARGET "${PROJECT_NAME}.elf"
    LINK_MAP "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map")

  # Enable ETL debug mode only for Debug builds
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(ETL_DEBUG)
  endif()
endmacro()
