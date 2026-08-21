#include "Adapters/node/filesystem/PathResolver.h"

#include <cassert>

int main() {
  using NodePath::Resolve;

  assert(Resolve("/sdcard", nullptr) == "/sdcard");
  assert(Resolve("/sdcard/projects", "song/song.pt") ==
         "/sdcard/projects/song/song.pt");
  assert(Resolve("/sdcard/projects", "./song/../song.pt") ==
         "/sdcard/projects/song.pt");
  assert(Resolve("/sdcard/projects", "../samples") == "/sdcard/samples");
  assert(Resolve("/sdcard", "../outside") == std::nullopt);
  assert(Resolve("/sdcard/projects", "../../outside") == std::nullopt);
  assert(Resolve("/sdcard/projects", "/projects/song.pt") ==
         "/sdcard/projects/song.pt");
  assert(Resolve("/sdcard/projects", "/sdcard/projects/song.pt") ==
         "/sdcard/projects/song.pt");
  assert(Resolve("/sdcard", "/sdcardevil/song.pt") ==
         "/sdcard/sdcardevil/song.pt");
  assert(Resolve("/sdcard", "/sdcard/../outside") == std::nullopt);

  return 0;
}
