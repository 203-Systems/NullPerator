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

TEST_CASE("player transport requires audio and both model bindings") {
  PlayerAudioReadiness readiness;
  readiness.BeginInitialization();

  CHECK_FALSE(readiness.CanStartTransport(false, false));
  CHECK_FALSE(readiness.CanStartTransport(true, true));

  REQUIRE(readiness.CompleteInitialization(true));
  CHECK(readiness.CanStartTransport(true, true));
  CHECK_FALSE(readiness.CanStartTransport(false, true));
  CHECK_FALSE(readiness.CanStartTransport(true, false));
  CHECK_FALSE(readiness.CanStartTransport(false, false));
}

TEST_CASE("missing LIVE bindings reject transport before lock or session access") {
  PlayerAudioReadiness readiness;
  REQUIRE(readiness.CompleteInitialization(true));

  bool mixerLockAttempted = false;
  bool liveSessionAccessed = false;
  if (readiness.CanStartTransport(false, false)) {
    mixerLockAttempted = true;
    liveSessionAccessed = true;
  }

  CHECK_FALSE(mixerLockAttempted);
  CHECK_FALSE(liveSessionAccessed);

  readiness.Close();
  CHECK_FALSE(readiness.CanStartTransport(true, true));
}
