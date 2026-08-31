#include "Application/Session/TrackerPlayerInitialization.h"

#include "doctest/doctest.h"

TEST_CASE("failed Player initialization is retried before project binding") {
  bool initialized = false;
  bool nextInitializationResult = false;
  int initializationCalls = 0;
  int bindingCalls = 0;

  const auto activate = [&]() {
    return tracker_session_detail::ActivatePlayer(
        initialized,
        [&]() {
          ++initializationCalls;
          return nextInitializationResult;
        },
        [&]() { ++bindingCalls; });
  };

  CHECK_FALSE(activate());
  CHECK_FALSE(initialized);
  CHECK(initializationCalls == 1);
  CHECK(bindingCalls == 0);

  nextInitializationResult = true;
  CHECK(activate());
  CHECK(initialized);
  CHECK(initializationCalls == 2);
  CHECK(bindingCalls == 0);

  CHECK(activate());
  CHECK(initializationCalls == 2);
  CHECK(bindingCalls == 1);
}
