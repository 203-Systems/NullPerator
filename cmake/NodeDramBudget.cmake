# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)

set(_PICOTRACKER_NODE_DRAM_CHECKER
    "${CMAKE_CURRENT_LIST_DIR}/../tools/check_node_dram_budget.py")

# Static data/BSS, including linker padding, must stay within 126 KiB.
# Task stacks and dynamic heap are measured separately at runtime.
# Raising this limit requires an explicit source change and memory review.
set(_PICOTRACKER_NODE_DRAM_BUDGET_BYTES 129024)

function(pico_tracker_add_node_dram_budget_check)
  set(options)
  set(one_value_args NAME FIRMWARE_TARGET LINK_MAP)
  cmake_parse_arguments(NODE_DRAM
                        "${options}"
                        "${one_value_args}"
                        ""
                        ${ARGN})

  if(NOT NODE_DRAM_NAME)
    set(NODE_DRAM_NAME node_dram_budget)
  endif()
  foreach(required_arg FIRMWARE_TARGET LINK_MAP)
    if(NOT NODE_DRAM_${required_arg})
      message(FATAL_ERROR
              "pico_tracker_add_node_dram_budget_check requires ${required_arg}")
    endif()
  endforeach()
  if(NOT TARGET "${NODE_DRAM_FIRMWARE_TARGET}")
    message(FATAL_ERROR
            "Node DRAM firmware target '${NODE_DRAM_FIRMWARE_TARGET}' does not exist")
  endif()

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_custom_target("${NODE_DRAM_NAME}" ALL
    COMMAND "${Python3_EXECUTABLE}"
            "${_PICOTRACKER_NODE_DRAM_CHECKER}"
            --link-map "${NODE_DRAM_LINK_MAP}"
            --budget-bytes "${_PICOTRACKER_NODE_DRAM_BUDGET_BYTES}"
    DEPENDS
      "${NODE_DRAM_FIRMWARE_TARGET}"
      "${_PICOTRACKER_NODE_DRAM_CHECKER}"
    COMMENT "Checking Node static data/BSS against the 126 KiB regression budget"
    USES_TERMINAL
    VERBATIM)
endfunction()
