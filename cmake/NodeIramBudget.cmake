# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)

set(_PICOTRACKER_NODE_IRAM_CHECKER
    "${CMAKE_CURRENT_LIST_DIR}/../tools/check_node_iram_budget.py")

# The ESP-IDF 5.3.5 baseline occupies 83,968 bytes (82 KiB), including vectors
# and linker alignment.  A 96 KiB product budget leaves 14 KiB (about 17%) for
# reviewed growth while catching accidental IRAM regressions.
# Raising this value is an explicit source change rather than a cache override.
set(_PICOTRACKER_NODE_IRAM_BUDGET_BYTES 98304)

function(pico_tracker_add_node_iram_budget_check)
  set(options)
  set(one_value_args NAME FIRMWARE_TARGET LINK_MAP)
  cmake_parse_arguments(NODE_IRAM
                        "${options}"
                        "${one_value_args}"
                        ""
                        ${ARGN})

  if(NOT NODE_IRAM_NAME)
    set(NODE_IRAM_NAME node_iram_budget)
  endif()
  foreach(required_arg FIRMWARE_TARGET LINK_MAP)
    if(NOT NODE_IRAM_${required_arg})
      message(FATAL_ERROR
              "pico_tracker_add_node_iram_budget_check requires ${required_arg}")
    endif()
  endforeach()
  if(NOT TARGET "${NODE_IRAM_FIRMWARE_TARGET}")
    message(FATAL_ERROR
            "Node IRAM firmware target '${NODE_IRAM_FIRMWARE_TARGET}' does not exist")
  endif()

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_custom_target("${NODE_IRAM_NAME}" ALL
    COMMAND "${Python3_EXECUTABLE}"
            "${_PICOTRACKER_NODE_IRAM_CHECKER}"
            --link-map "${NODE_IRAM_LINK_MAP}"
            --budget-bytes "${_PICOTRACKER_NODE_IRAM_BUDGET_BYTES}"
    DEPENDS
      "${NODE_IRAM_FIRMWARE_TARGET}"
      "${_PICOTRACKER_NODE_IRAM_CHECKER}"
    COMMENT "Checking Node instruction RAM against the 96 KiB regression budget"
    USES_TERMINAL
    VERBATIM)
endfunction()
