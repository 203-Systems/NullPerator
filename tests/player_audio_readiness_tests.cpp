#include "Application/Player/PlayerAudioReadiness.h"

#include "doctest/doctest.h"

TEST_CASE("player audio readiness closes every incomplete lifecycle") {
  PlayerAudioReadiness readiness;
  CHECK_FALSE(readiness.IsReady());

  readiness.BeginInitialization();
  CHECK_FALSE(readiness.IsReady());
  CHECK_FALSE(readiness.CompleteInitialization(false));
  CHECK_FALSE(readiness.IsReady());

  CHECK(readiness.CompleteInitialization(true));
  CHECK(readiness.IsReady());

  readiness.BeginInitialization();
  CHECK_FALSE(readiness.IsReady());
  CHECK(readiness.CompleteInitialization(true));
  readiness.Close();
  CHECK_FALSE(readiness.IsReady());
}
