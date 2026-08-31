#include "Application/Model/Groove.h"

#include "doctest/doctest.h"

#include <atomic>
#include <cstdint>
#include <thread>

TEST_CASE("groove telemetry never pairs a selection with another row") {
  Groove *groove = Groove::GetInstance();
  groove->Clear();

  unsigned char *steps = groove->GetGrooveData(1);
  steps[0] = 1U;
  steps[1] = 1U;
  steps[2] = NO_GROOVE_DATA;
  groove->SetGroove(0, 2);

  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (std::uint32_t iteration = 0U; iteration < 50000U; ++iteration) {
      // The only published states are (1, 0), (1, 1), and (2, 0). A reader
      // seeing (2, 1) has combined fields from opposite sides of SetGroove().
      groove->SetGroove(0, 1);
      groove->Trigger();
      groove->Trigger();
      groove->SetGroove(0, 2);
    }
    done.store(true, std::memory_order_release);
  });

  bool consistent = true;
  std::uint32_t reads = 0U;
  do {
    int selected = -1;
    int position = -1;
    groove->GetChannelData(0, &selected, &position);
    const bool allowed = (selected == 1 && (position == 0 || position == 1)) ||
                         (selected == 2 && position == 0);
    if (!allowed) {
      consistent = false;
      break;
    }
    ++reads;
  } while (!done.load(std::memory_order_acquire));

  writer.join();
  groove->Clear();
  CHECK(consistent);
  CHECK(reads > 0U);
}
