/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include <atomic>
#include <cstddef>

// Control calls Stop(), wakes blocked queues, and waits for IsIdle() before
// resetting buffers or deleting tasks. One owner calls control methods; each
// worker owns one busy slot. Sequential consistency prevents the
// stop/enter handshake from having both sides observe the other's old value.
template <std::size_t WorkerCount> class WorkerGate {
  static_assert(WorkerCount > 0);

public:
  bool TryEnter(std::size_t worker) {
    busy_[worker].store(true);
    if (running_.load())
      return true;
    busy_[worker].store(false);
    return false;
  }
  void Leave(std::size_t worker) { busy_[worker].store(false); }
  void Start() { running_.store(true); }
  void Stop() { running_.store(false); }
  bool IsRunning() const { return running_.load(); }
  bool IsIdle() const {
    for (const auto &busy : busy_)
      if (busy.load())
        return false;
    return true;
  }

  // Use a Guard when early returns may occur before doing any work.
  class Guard {
  public:
    Guard(WorkerGate &gate, std::size_t worker)
        : gate_(gate), worker_(worker), entered_(gate.TryEnter(worker)) {}
    ~Guard() {
      if (entered_)
        gate_.Leave(worker_);
    }
    explicit operator bool() const { return entered_; }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

  private:
    WorkerGate &gate_;
    std::size_t worker_;
    bool entered_;
  };

  class Lease {
  public:
    Lease(WorkerGate &gate, std::size_t worker)
        : gate_(gate), worker_(worker) {}
    ~Lease() { gate_.Leave(worker_); }
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;

  private:
    WorkerGate &gate_;
    std::size_t worker_;
  };

private:
  std::atomic<bool> running_{false};
  std::atomic<bool> busy_[WorkerCount]{};
};
