if(NOT DEFINED WASM_BINARY OR NOT EXISTS "${WASM_BINARY}")
  message(FATAL_ERROR "A valid WASM_BINARY is required")
endif()
if(NOT DEFINED LINK_MAP OR NOT EXISTS "${LINK_MAP}")
  message(FATAL_ERROR "A valid LINK_MAP is required")
endif()

file(READ "${LINK_MAP}" link_map)
set(required_objects
  "Ui2TrackerApplication.cpp.o"
  "Player.cpp.o"
  "UiSongView.cpp.o")
foreach(required_object IN LISTS required_objects)
  string(FIND "${link_map}" "${required_object}" object_position)
  if(object_position EQUAL -1)
    message(FATAL_ERROR
      "WASM core link-closure check failed: missing ${required_object}")
  endif()
endforeach()

message(STATUS "Verified native UI2 core link closure")
