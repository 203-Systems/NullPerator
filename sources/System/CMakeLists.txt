add_subdirectory(System)
add_subdirectory(io)
add_subdirectory(Console)
add_subdirectory(Process)
add_subdirectory(FileSystem)
add_subdirectory(Timer)
add_subdirectory(Profiler)

# The strict UI2 firmware presents RGB565 frames directly and must not compile
# the legacy character Remote UI protocol.  Keep it available only to host
# builds and the explicitly selected legacy-reference firmware product.
if(NOT DEFINED PICOTRACKER_UI OR NOT PICOTRACKER_UI STREQUAL "ui2")
  add_subdirectory(RemoteUI)
endif()
