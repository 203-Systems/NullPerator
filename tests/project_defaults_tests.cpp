#include "doctest/doctest.h"

#include "Application/Model/ProjectDefaults.h"

TEST_CASE("project master volume default is platform independent") {
  CHECK(DEFAULT_MASTER_VOLUME == 60);
}
