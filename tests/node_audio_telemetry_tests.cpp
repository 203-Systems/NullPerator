#include "Adapters/node/audio/AudioTelemetry.h"
#include "Foundation/Concurrency/WorkerGate.h"
#include "doctest/doctest.h"
#include <thread>

TEST_CASE(
    "Node audio telemetry compares elapsed time with actual frame budget") {
  NodeAudioTelemetry metrics;
  metrics.RecordRender(1000, 0);
  CHECK(metrics.Read().blocks == 0);
  metrics.RecordRender(8000, 441);
  metrics.RecordRender(10000, 441);
  CHECK(metrics.Read().deadlineMisses == 0);
  metrics.RecordRender(10001, 441);
  metrics.RecordStarvation();
  metrics.RecordWriteError();
  const auto snapshot = metrics.Read();
  CHECK(snapshot.blocks == 3);
  CHECK(snapshot.frames == 1323);
  CHECK(snapshot.maxRenderUs == 10001);
  CHECK(snapshot.maxLoadPermille == 1001);
  CHECK(snapshot.deadlineMisses == 1);
  CHECK(snapshot.producerStarvations == 1);
  CHECK(snapshot.writeErrors == 1);
}

TEST_CASE("Node audio telemetry avoids 32-bit overflow on long stalls") {
  NodeAudioTelemetry metrics;
  metrics.RecordRender(1000000, 1875);
  CHECK(metrics.Read().deadlineMisses == 1);
  CHECK(metrics.Read().maxLoadPermille == 23520);
}

TEST_CASE("Node stopping waits for both borrowed audio buffers") {
  WorkerGate<2> gate;
  CHECK_FALSE(gate.TryEnter(0));
  gate.Start();
  REQUIRE(gate.TryEnter(0));
  REQUIRE(gate.TryEnter(1));
  gate.Stop();
  CHECK_FALSE(gate.IsIdle());
  gate.Leave(0);
  CHECK_FALSE(gate.IsIdle());
  gate.Leave(1);
  CHECK(gate.IsIdle());
  CHECK_FALSE(gate.TryEnter(0));
  gate.Start();
  REQUIRE(gate.TryEnter(0));
  {
    WorkerGate<2>::Lease lease(gate, 0);
    CHECK_FALSE(gate.IsIdle());
  }
  CHECK(gate.IsIdle());
}

TEST_CASE("Node audio control cannot reset buffers while a worker owns them") {
  WorkerGate<2> gate;
  std::atomic<bool> quit{false};
  // Deliberately ordinary storage: sanitizer/TSan can detect a missing
  // stop/enter happens-before edge, in addition to the explicit invariant.
  int buffers[2]{};
  auto worker = [&](std::size_t index) {
    while (!quit.load()) {
      if (!gate.TryEnter(index)) {
        std::this_thread::yield();
        continue;
      }
      WorkerGate<2>::Lease lease(gate, index);
      ++buffers[index];
    }
  };
  std::thread render(worker, 0), writer(worker, 1);
  for (int iteration = 0; iteration < 2000; ++iteration) {
    gate.Start();
    std::this_thread::yield();
    gate.Stop();
    while (!gate.IsIdle())
      std::this_thread::yield();
    buffers[0] = buffers[1] = 0;
  }
  quit.store(true);
  render.join();
  writer.join();
  CHECK(buffers[0] == 0);
  CHECK(buffers[1] == 0);
}

TEST_CASE("Single capture worker drains auto-finish before a new destination") {
  WorkerGate<1> gate;
  int destination = 0;
  gate.Start();
  std::atomic<bool> full{false}, release{false};
  std::thread callback([&] {
    WorkerGate<1>::Guard capture(gate, 0);
    if (!capture)
      return;
    destination = 42;
    gate.Stop(); // Capacity reached, but callback still owns the old take.
    full.store(true);
    while (!release.load())
      std::this_thread::yield();
    destination = 43;
  });
  while (!full.load())
    std::this_thread::yield();
  CHECK_FALSE(gate.IsRunning());
  CHECK_FALSE(gate.IsIdle());
  release.store(true);
  while (!gate.IsIdle())
    std::this_thread::yield();
  CHECK(destination == 43);
  destination = 0;
  gate.Start();
  callback.join();
  {
    WorkerGate<1>::Guard capture(gate, 0);
    REQUIRE(capture);
    destination = 7;
  }
  gate.Stop();
  CHECK(gate.IsIdle());
  CHECK(destination == 7);
}
