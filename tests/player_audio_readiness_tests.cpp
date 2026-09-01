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
  CHECK_FALSE(player_audio_readiness::CanStartTransport(false, false, false));
  CHECK_FALSE(player_audio_readiness::CanStartTransport(false, false, true));
  CHECK_FALSE(player_audio_readiness::CanStartTransport(false, true, false));
  CHECK_FALSE(player_audio_readiness::CanStartTransport(false, true, true));
  CHECK_FALSE(player_audio_readiness::CanStartTransport(true, false, false));
  CHECK_FALSE(player_audio_readiness::CanStartTransport(true, false, true));
  CHECK_FALSE(player_audio_readiness::CanStartTransport(true, true, false));
  CHECK(player_audio_readiness::CanStartTransport(true, true, true));
}
