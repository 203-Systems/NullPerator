add_library(ui2
  Chrome/UiBarResolver.cpp
  Chrome/UiChromeRenderer.cpp
  Render/UiFrameRenderer.cpp
  Render/UiDirtyTiles.cpp
  Render/UiIndexedSurface.cpp
  Render/UiRasterizer.cpp
  Render/UiRgb565Presenter.cpp
  Render/UiVuGradient.cpp
  Text/UiFont5x7.cpp
  Theme/UiPalette.cpp
  Views/Phrase/UiPhraseView.cpp
  Views/Song/UiSongView.cpp
  UiEngine.cpp)

target_include_directories(ui2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
