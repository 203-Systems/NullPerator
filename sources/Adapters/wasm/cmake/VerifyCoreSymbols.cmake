if(NOT DEFINED JS_GLUE OR NOT EXISTS "${JS_GLUE}")
  message(FATAL_ERROR "A valid JS_GLUE is required")
endif()
if(NOT DEFINED WASM_BINARY OR NOT EXISTS "${WASM_BINARY}")
  message(FATAL_ERROR "A valid WASM_BINARY is required")
endif()
if(NOT DEFINED LINK_MAP OR NOT EXISTS "${LINK_MAP}")
  message(FATAL_ERROR "A valid LINK_MAP is required")
endif()

file(READ "${JS_GLUE}" javascript_glue)
set(required_anchors
  "PicoTracker_Wasm_CoreApplicationAnchor"
  "PicoTracker_Wasm_CoreAppWindowAnchor"
  "PicoTracker_Wasm_CorePlayerAnchor"
  "PicoTracker_Wasm_CoreViewAnchor")
foreach(required_anchor IN LISTS required_anchors)
  string(FIND "${javascript_glue}" "${required_anchor}" anchor_position)
  if(anchor_position EQUAL -1)
    message(FATAL_ERROR
      "WASM core link-closure check failed: missing ${required_anchor}")
  endif()
endforeach()

file(READ "${LINK_MAP}" link_map)
set(required_objects
  "Application.cpp.o"
  "AppWindow.cpp.o"
  "Player.cpp.o"
  "SongView.cpp.o")
foreach(required_object IN LISTS required_objects)
  string(FIND "${link_map}" "${required_object}" object_position)
  if(object_position EQUAL -1)
    message(FATAL_ERROR
      "WASM core link-closure check failed: missing ${required_object}")
  endif()
endforeach()

message(STATUS
  "Verified stable core anchors and representative extracted objects")
