# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)

# Opt-in helper for the future UI2-only product target.  This file is not
# included by the current hybrid Node build and the target is intentionally not
# added to ALL.  The caller must pass final build artifacts from a clean build.
set(_PICOTRACKER_UI2_ONLY_CHECKER
    "${CMAKE_CURRENT_LIST_DIR}/../tools/verify_ui2_only_firmware.py")

function(pico_tracker_add_ui2_only_acceptance_check)
  set(options)
  set(one_value_args
      NAME
      FIRMWARE_TARGET
      COMPILE_COMMANDS
      LINK_MAP
      ELF
      NM_TOOL)
  cmake_parse_arguments(UI2_ONLY
                        "${options}"
                        "${one_value_args}"
                        ""
                        ${ARGN})

  if(NOT UI2_ONLY_NAME)
    set(UI2_ONLY_NAME ui2_only_firmware_acceptance)
  endif()
  foreach(required_arg FIRMWARE_TARGET COMPILE_COMMANDS LINK_MAP ELF)
    if(NOT UI2_ONLY_${required_arg})
      message(FATAL_ERROR
              "pico_tracker_add_ui2_only_acceptance_check requires ${required_arg}")
    endif()
  endforeach()
  if(NOT TARGET "${UI2_ONLY_FIRMWARE_TARGET}")
    message(FATAL_ERROR
            "UI2-only firmware target '${UI2_ONLY_FIRMWARE_TARGET}' does not exist")
  endif()

  if(NOT UI2_ONLY_NM_TOOL)
    set(UI2_ONLY_NM_TOOL "${CMAKE_NM}")
  endif()
  if(NOT UI2_ONLY_NM_TOOL)
    message(FATAL_ERROR
            "UI2-only acceptance needs NM_TOOL or a toolchain CMAKE_NM")
  endif()

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_custom_target("${UI2_ONLY_NAME}"
    COMMAND "${Python3_EXECUTABLE}"
            "${_PICOTRACKER_UI2_ONLY_CHECKER}"
            --require-complete
            --compile-commands "${UI2_ONLY_COMPILE_COMMANDS}"
            --link-map "${UI2_ONLY_LINK_MAP}"
            --elf "${UI2_ONLY_ELF}"
            --nm-tool "${UI2_ONLY_NM_TOOL}"
    DEPENDS "${UI2_ONLY_FIRMWARE_TARGET}"
    COMMENT "Verifying strict UI2-only firmware compile and link contract"
    USES_TERMINAL
    VERBATIM)
endfunction()
