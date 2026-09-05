#include "Adapters/ios/timer/IOSTimer.h"
#include "doctest/doctest.h"
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace {
class TimerObserver final : public I_Observer {
public:
  void Update(Observable &observable, I_ObservableData *) override {
    auto &timer = static_cast<IOSTimer &>(observable);
    std::lock_guard lock(mutex);
    ++calls;
    if (restart && calls == 1) {
      timer.Stop();
      restarted = timer.Start();
    } else {
      timer.Stop();
    }
    wake.notify_all();
  }
  bool Wait(int count) {
    std::unique_lock lock(mutex);
    return wake.wait_for(lock, std::chrono::seconds(2),
                         [&] { return calls >= count; });
  }
  std::mutex mutex;
  std::condition_variable wake;
  int calls = 0;
  bool restart = false;
  bool restarted = false;
};
} // namespace

TEST_CASE("iOS timer rejects non-finite and unrepresentable periods") {
  IOSTimer timer;
  for (float value : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::max()}) {
    timer.SetPeriod(value);
    CHECK_FALSE(timer.Start());
    CHECK(timer.GetPeriod() == -1.0F);
  }
}

TEST_CASE(
    "iOS timer reschedules a running deadline and can restart in callback") {
  TimerObserver observer;
  IOSTimer timer;
  timer.AddObserver(observer);
  observer.restart = true;
  timer.SetPeriod(60000.0F);
  REQUIRE(timer.Start());
  timer.SetPeriod(1.0F);
  CHECK(observer.Wait(2));
  timer.Stop();
  CHECK(observer.calls == 2);
  CHECK(observer.restarted);
  observer.restart = false;
  REQUIRE(timer.Start());
  CHECK(observer.Wait(3));
  timer.Stop();
  CHECK(observer.calls == 3);
}
