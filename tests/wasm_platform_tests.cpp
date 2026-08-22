#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Adapters/wasm/timer/WasmTimer.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {
class CountingObserver final : public I_Observer {
public:
  void Update(Observable &, I_ObservableData *) override { ++updates; }

  int updates = 0;
};

class PeriodChangingObserver final : public I_Observer {
public:
  PeriodChangingObserver(WasmTimer &timer, float period)
      : timer_(timer), period_(period) {}

  void Update(Observable &, I_ObservableData *) override {
    timer_.SetPeriod(period_);
  }

private:
  WasmTimer &timer_;
  float period_;
};

class ScopedFilesystemFixture {
public:
  ScopedFilesystemFixture() {
    static std::uint32_t sequence = 0;
    const std::string suffix = std::to_string(++sequence);
    root = std::filesystem::temp_directory_path() /
           ("picotracker-wasm-fs-root-" + suffix);
    outside = std::filesystem::temp_directory_path() /
              ("picotracker-wasm-fs-outside-" + suffix);
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);
  }

  ~ScopedFilesystemFixture() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::remove_all(outside, error);
  }

  std::filesystem::path root;
  std::filesystem::path outside;
};
} // namespace

TEST_CASE("WASM monotonic clock converts milliseconds without moving backwards") {
  double now = 12.5;
  WasmClock clock([&now] { return now; });

  CHECK(clock.Micros() == 12500);
  CHECK(clock.Millis() == 12);

  now = 12.25;
  CHECK(clock.Micros() == 12500);
  CHECK(clock.Millis() == 12);
}

TEST_CASE("a stale WASM timer callback cannot fire after restart") {
  std::vector<std::function<void()>> scheduledCallbacks;
  std::vector<std::uint32_t> cancelledTimers;
  WasmTimer timer(
      [&scheduledCallbacks](double, std::function<void()> callback) {
        scheduledCallbacks.push_back(std::move(callback));
        return static_cast<std::uint32_t>(scheduledCallbacks.size());
      },
      [&cancelledTimers](std::uint32_t timerId) {
        cancelledTimers.push_back(timerId);
      });
  CountingObserver observer;
  timer.AddObserver(observer);

  timer.SetPeriod(25.0F);
  REQUIRE(timer.Start());
  REQUIRE(scheduledCallbacks.size() == 1);
  const auto staleCallback = scheduledCallbacks[0];
  timer.Stop();
  REQUIRE(timer.Start());

  REQUIRE(cancelledTimers.size() == 1);
  CHECK(cancelledTimers[0] == 1);
  staleCallback();
  CHECK(observer.updates == 0);
}

TEST_CASE("changing a running WASM timer period reschedules immediately") {
  std::vector<double> delays;
  std::vector<std::function<void()>> callbacks;
  std::vector<std::uint32_t> cancellations;
  WasmTimer timer(
      [&delays, &callbacks](double delay, std::function<void()> callback) {
        delays.push_back(delay);
        callbacks.push_back(std::move(callback));
        return static_cast<std::uint32_t>(callbacks.size());
      },
      [&cancellations](std::uint32_t timerId) {
        cancellations.push_back(timerId);
      });

  timer.SetPeriod(25.0F);
  REQUIRE(timer.Start());
  timer.SetPeriod(10.0F);

  REQUIRE(delays.size() == 2);
  CHECK(delays[0] == doctest::Approx(25.0));
  CHECK(delays[1] == doctest::Approx(10.0));
  REQUIRE(cancellations.size() == 1);
  CHECK(cancellations[0] == 1);
}

TEST_CASE("changing a WASM timer period from its callback safely rearms") {
  std::vector<double> delays;
  std::vector<std::function<void()>> callbacks;
  WasmTimer timer(
      [&delays, &callbacks](double delay, std::function<void()> callback) {
        delays.push_back(delay);
        callbacks.push_back(std::move(callback));
        return static_cast<std::uint32_t>(callbacks.size());
      },
      [](std::uint32_t) {});
  PeriodChangingObserver observer(timer, 8.0F);
  timer.AddObserver(observer);

  timer.SetPeriod(25.0F);
  REQUIRE(timer.Start());
  REQUIRE(callbacks.size() == 1);
  callbacks[0]();

  REQUIRE(delays.size() == 2);
  CHECK(delays[0] == doctest::Approx(25.0));
  CHECK(delays[1] == doctest::Approx(8.0));
}

TEST_CASE("WASM filesystem rejects lexical parent traversal") {
  ScopedFilesystemFixture fixture;
  WasmFileSystem filesystem(fixture.root.string());

  CHECK_FALSE(filesystem.Open("../escaped.txt", "wb"));
  CHECK_FALSE(std::filesystem::exists(fixture.root.parent_path() /
                                      "escaped.txt"));
}

TEST_CASE("WASM filesystem rejects symlinks that leave its root") {
  ScopedFilesystemFixture fixture;
  const auto link = fixture.root / "outside-link";
  std::filesystem::create_directory_symlink(fixture.outside, link);
  WasmFileSystem filesystem(fixture.root.string());

  CHECK_FALSE(filesystem.chdir("/outside-link"));
  CHECK_FALSE(filesystem.Open("/outside-link/escaped.txt", "wb"));
  CHECK_FALSE(std::filesystem::exists(fixture.outside / "escaped.txt"));
}
