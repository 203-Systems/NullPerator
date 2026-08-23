add_library(ui2
  Render/UiDirtyTiles.cpp
  Render/UiIndexedSurface.cpp
  Render/UiRasterizer.cpp
  Theme/UiPalette.cpp
  UiEngine.cpp)

target_include_directories(ui2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
