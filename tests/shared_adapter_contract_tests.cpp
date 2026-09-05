#include "Adapters/posix/filesystem/PosixFile.h"
#include "doctest/doctest.h"
#include <cstdio>
#include <unistd.h>

TEST_CASE("Native sync errors are distinct from browser buffered completion") {
  int descriptors[2]{};
  REQUIRE(::pipe(descriptors) == 0);
  {
    PosixFile native(::fdopen(descriptors[1], "w"));
    REQUIRE(native.Write("x", 1, 1) == 1);
    CHECK_FALSE(native.Sync()); // A pipe can flush, but cannot fsync.
  }
  ::close(descriptors[0]);
  REQUIRE(::pipe(descriptors) == 0);
  {
    PosixFile browser(::fdopen(descriptors[1], "w"), false,
                      {StoragePolicy::SyncMode::Buffered, nullptr});
    REQUIRE(browser.Write("x", 1, 1) == 1);
    CHECK(browser.Sync());
  }
  ::close(descriptors[0]);
}
