#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Adapters/wasm/filesystem/WasmStorageBridge.h"
#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Adapters/wasm/timer/WasmTimer.h"
#include "System/FileSystem/CopyFileJournal.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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

std::string CopyJournalPath(const char *destination, const char *prefix) {
  const std::size_t capacity =
      FileCopyJournal::SiblingPathCapacity(destination, prefix);
  REQUIRE(capacity > 1U);
  std::string path(capacity, '\0');
  REQUIRE(FileCopyJournal::BuildSiblingPath(destination, prefix, path.data(),
                                            path.size()));
  path.resize(capacity - 1U);
  return path;
}

std::filesystem::path HostPath(const ScopedFilesystemFixture &fixture,
                               const std::string &logicalPath) {
  return fixture.root / std::filesystem::path(logicalPath).relative_path();
}

struct InputQueueFixture {
  static bool Queue(std::uint16_t action, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex);
    if (fail) {
      return false;
    }
    events.emplace_back(action, pressed);
    return true;
  }

  static void Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    fail = false;
    events.clear();
  }

  static std::mutex mutex;
  static bool fail;
  static std::vector<std::pair<std::uint16_t, bool>> events;
};

std::mutex InputQueueFixture::mutex;
bool InputQueueFixture::fail = false;
std::vector<std::pair<std::uint16_t, bool>> InputQueueFixture::events;

std::uint32_t storageMutationNotifications = 0;
void CountStorageMutation() { ++storageMutationNotifications; }
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

TEST_CASE("WASM filesystem contains absolute copy and move destinations") {
  ScopedFilesystemFixture fixture;
  WasmFileSystem filesystem(fixture.root.string());
  CHECK_FALSE(filesystem.DeleteDir("/"));
  {
    auto source = filesystem.Open("/project/source.dat", "wb");
    REQUIRE(source);
    REQUIRE(source->Write("x", 1, 1) == 1);
  }
  const auto outsideLink = fixture.root / "outside-link";
  std::filesystem::create_directory_symlink(fixture.outside, outsideLink);

  CHECK_FALSE(filesystem.CopyFile("/project/source.dat", "/../escaped.dat"));
  CHECK_FALSE(filesystem.CopyFile("/project/source.dat", "/outside-link/copied.dat"));
  CHECK_FALSE(filesystem.MoveFile("/project/source.dat", "/outside-link/moved.dat"));
  CHECK(std::filesystem::exists(fixture.root / "project" / "source.dat"));
}

TEST_CASE("WASM filesystem only notifies persistence after successful mutations") {
  ScopedFilesystemFixture fixture;
  WasmFileSystem filesystem(fixture.root.string());
  storageMutationNotifications = 0;
  WasmStorage_SetMutationNotifierForTesting(&CountStorageMutation);

  CHECK_FALSE(filesystem.DeleteFile("/missing.dat"));
  CHECK(filesystem.makeDir("/project", true));
  CHECK(storageMutationNotifications == 1);
  CHECK(filesystem.makeDir("/project", true));
  CHECK(storageMutationNotifications == 1);
  {
    auto file = filesystem.Open("/project/save.dat", "wb");
    REQUIRE(file);
    CHECK(file->Write("ok", 1, 2) == 2);
    CHECK(storageMutationNotifications == 1);
    CHECK(file->Sync());
    CHECK(storageMutationNotifications == 2);
  }
  CHECK(storageMutationNotifications == 2);
  CHECK(filesystem.CopyFile("/project/save.dat", "/project/copy.dat"));
  CHECK(storageMutationNotifications == 3);
  CHECK(filesystem.MoveFile("/project/copy.dat", "/project/moved.dat"));
  CHECK(storageMutationNotifications == 4);
  CHECK(filesystem.DeleteFile("/project/moved.dat"));
  CHECK(storageMutationNotifications == 5);
  {
    auto empty = filesystem.Open("/project/empty.dat", "wb");
    REQUIRE(empty);
  }
  CHECK(storageMutationNotifications == 6);

  WasmStorage_SetMutationNotifierForTesting(nullptr);
}

TEST_CASE("WASM filesystem rolls back failed parent creation without persistence noise") {
  ScopedFilesystemFixture fixture;
  WasmFileSystem filesystem(fixture.root.string());
  storageMutationNotifications = 0;
  WasmStorage_SetMutationNotifierForTesting(&CountStorageMutation);

  CHECK_FALSE(filesystem.Open("/read-plus-parent/missing.dat", "r+"));
  CHECK_FALSE(std::filesystem::exists(fixture.root / "read-plus-parent"));

  std::filesystem::create_directory(fixture.root / "invalid-open-target");
  CHECK_FALSE(filesystem.Open("/invalid-open-target", "wb"));
  CHECK(std::filesystem::is_directory(fixture.root / "invalid-open-target"));
  CHECK(storageMutationNotifications == 0);

  CHECK_FALSE(filesystem.CopyFile("/missing.dat", "/copy-parent/out.dat"));
  CHECK_FALSE(filesystem.MoveFile("/missing.dat", "/move-parent/out.dat"));
  CHECK_FALSE(std::filesystem::exists(fixture.root / "copy-parent"));
  CHECK_FALSE(std::filesystem::exists(fixture.root / "move-parent"));

  {
    std::ofstream existing(fixture.root / "existing.dat", std::ios::binary);
    existing << "keep-me";
  }
  CHECK_FALSE(filesystem.CopyFile("/missing.dat", "/existing.dat"));
  {
    std::ifstream existing(fixture.root / "existing.dat", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(existing), {}) ==
          "keep-me");
  }

  {
    std::ofstream source(fixture.root / "source.dat", std::ios::binary);
    source << "x";
  }
  std::filesystem::create_directory(fixture.root / "invalid-destination");
  CHECK_FALSE(filesystem.CopyFile("/source.dat", "/invalid-destination"));
  CHECK_FALSE(filesystem.MoveFile("/source.dat", "/invalid-destination"));
  CHECK(std::filesystem::exists(fixture.root / "source.dat"));
  CHECK(storageMutationNotifications == 0);

  WasmStorage_SetMutationNotifierForTesting(nullptr);
}

TEST_CASE("WASM copy overwrite preserves old bytes when journal preparation fails") {
  ScopedFilesystemFixture fixture;
  WasmFileSystem filesystem(fixture.root.string());
  {
    std::ofstream source(fixture.root / "source.dat", std::ios::binary);
    source << "new-bytes";
    std::ofstream destination(fixture.root / "target.dat", std::ios::binary);
    destination << "old-bytes";
  }

  const std::string temporary =
      CopyJournalPath("/target.dat", FileCopyJournal::TempPrefix);
  const std::filesystem::path blocked = HostPath(fixture, temporary);
  std::filesystem::create_directory(blocked);
  {
    std::ofstream blocker(blocked / "keep");
    blocker << "reserved";
  }

  CHECK_FALSE(filesystem.CopyFile("/source.dat", "/target.dat"));
  std::ifstream destination(fixture.root / "target.dat", std::ios::binary);
  CHECK(std::string(std::istreambuf_iterator<char>(destination), {}) ==
        "old-bytes");
}

TEST_CASE("WASM copy recovers an interrupted backup and avoids legacy suffix collision") {
  ScopedFilesystemFixture fixture;
  WasmFileSystem filesystem(fixture.root.string());
  {
    std::ofstream source(fixture.root / "source.dat", std::ios::binary);
    source << "new-bytes";
    std::ofstream legacy(fixture.root / "target.dat.copy.tmp",
                         std::ios::binary);
    legacy << "user-file";
  }

  const std::string temporary =
      CopyJournalPath("/target.dat", FileCopyJournal::TempPrefix);
  const std::string backup =
      CopyJournalPath("/target.dat", FileCopyJournal::BackupPrefix);
  {
    std::ofstream interruptedTemp(HostPath(fixture, temporary),
                                  std::ios::binary);
    interruptedTemp << "partial";
    std::ofstream interruptedBackup(HostPath(fixture, backup),
                                    std::ios::binary);
    interruptedBackup << "old-bytes";
  }

  CHECK(filesystem.CopyFile("/source.dat", "/target.dat"));
  {
    std::ifstream destination(fixture.root / "target.dat", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(destination), {}) ==
          "new-bytes");
  }
  {
    std::ifstream legacy(fixture.root / "target.dat.copy.tmp",
                         std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(legacy), {}) ==
          "user-file");
  }
  CHECK_FALSE(std::filesystem::exists(HostPath(fixture, temporary)));
  CHECK_FALSE(std::filesystem::exists(HostPath(fixture, backup)));
}

TEST_CASE("WASM action queue orders concurrent releases after accepted presses") {
  InputQueueFixture::Reset();
  InputMap::SetQueueForTesting(&InputQueueFixture::Queue);
  InputMap::ReleaseAllActions();

  std::thread press([] { InputMap::SetAction(3, true); });
  std::thread release([] { InputMap::ReleaseAllActions(); });
  press.join();
  release.join();
  InputMap::ReleaseAllActions();

  bool pressed = false;
  for (const auto &[action, isPressed] : InputQueueFixture::events) {
    if (action != 3) {
      continue;
    }
    if (isPressed) {
      pressed = true;
    } else {
      CHECK(pressed);
    }
  }
  CHECK(InputMap::GetHeldActionMask() == 0);
  InputMap::ResetQueueForTesting();
}

TEST_CASE("WASM semantic action edges are idempotent and reserved ids reject") {
  InputQueueFixture::Reset();
  InputMap::SetQueueForTesting(&InputQueueFixture::Queue);
  InputMap::ReleaseAllActions();

  constexpr std::uint16_t shift =
      static_cast<std::uint16_t>(TrackerAction::Shift);
  REQUIRE(InputMap::SetAction(shift, true));
  REQUIRE(InputMap::SetAction(shift, true));
  REQUIRE(InputMap::SetAction(shift, false));
  REQUIRE(InputMap::SetAction(shift, false));

  REQUIRE(InputQueueFixture::events.size() == 2U);
  CHECK(InputQueueFixture::events[0].first == shift);
  CHECK(InputQueueFixture::events[0].second);
  CHECK(InputQueueFixture::events[1].first == shift);
  CHECK_FALSE(InputQueueFixture::events[1].second);
  CHECK(InputMap::GetHeldActionMask() == 0U);

  CHECK_FALSE(InputMap::SetAction(
      static_cast<std::uint16_t>(TrackerAction::Reserved8), true));
  CHECK_FALSE(InputMap::SetAction(
      static_cast<std::uint16_t>(TrackerAction::Reserved9), true));
  CHECK(InputQueueFixture::events.size() == 2U);
  InputMap::ResetQueueForTesting();
}

TEST_CASE("WASM held directions accept repeat down edges without changing hold state") {
  InputQueueFixture::Reset();
  InputMap::SetQueueForTesting(&InputQueueFixture::Queue);
  InputMap::ReleaseAllActions();

  constexpr std::uint16_t down =
      static_cast<std::uint16_t>(TrackerAction::Down);
  REQUIRE(InputMap::SetAction(down, true));
  REQUIRE(InputMap::RepeatAction(down));
  CHECK(InputMap::GetHeldActionMask() == (1U << down));
  REQUIRE(InputQueueFixture::events.size() == 2U);
  CHECK(InputQueueFixture::events[1] == std::pair{down, true});
  CHECK_FALSE(InputMap::RepeatAction(
      static_cast<std::uint16_t>(TrackerAction::Enter)));

  REQUIRE(InputMap::SetAction(down, false));
  CHECK_FALSE(InputMap::RepeatAction(down));
  InputMap::ResetQueueForTesting();
}

TEST_CASE("WASM action queue retains failed releases until they can be queued") {
  InputQueueFixture::Reset();
  InputMap::SetQueueForTesting(&InputQueueFixture::Queue);
  InputMap::ReleaseAllActions();

  REQUIRE(InputMap::SetAction(6, true));
  InputQueueFixture::fail = true;
  InputMap::ReleaseAllActions();
  CHECK(InputMap::GetHeldActionMask() == (1u << 6));

  InputQueueFixture::fail = false;
  InputMap::ReleaseAllActions();
  CHECK(InputMap::GetHeldActionMask() == 0);
  REQUIRE(InputQueueFixture::events.size() == 2);
  CHECK(InputQueueFixture::events[0].first == 6);
  CHECK(InputQueueFixture::events[0].second);
  CHECK(InputQueueFixture::events[1].first == 6);
  CHECK_FALSE(InputQueueFixture::events[1].second);
  InputMap::ResetQueueForTesting();
}

TEST_CASE("WASM frame pump retries an ordinary failed action release") {
  InputQueueFixture::Reset();
  InputMap::SetQueueForTesting(&InputQueueFixture::Queue);
  InputMap::ReleaseAllActions();

  REQUIRE(InputMap::SetAction(3, true));
  InputQueueFixture::fail = true;
  CHECK_FALSE(InputMap::SetAction(3, false));
  CHECK(InputMap::GetHeldActionMask() == (1u << 3));

  InputQueueFixture::fail = false;
  InputMap::RetryPendingTransitions();
  CHECK(InputMap::GetHeldActionMask() == 0);
  REQUIRE(InputQueueFixture::events.size() == 2);
  CHECK(InputQueueFixture::events[1].first == 3);
  CHECK_FALSE(InputQueueFixture::events[1].second);
  InputMap::ResetQueueForTesting();
}
