/* SPDX-License-Identifier: BSD-3-Clause */

#include "InputFrameLatencyTracker.h"

#include "System/Console/Profiler.h"
#include "System/Console/TraceRecord.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>

namespace {
enum class TicketState : std::uint8_t {
  Empty = 0,
  Writing,
  Accepted,
  Dispatched,
  Reading,
};

struct TicketSlot {
  // The version prevents an ABA when a browser producer reuses a slot while
  // the application pthread is selecting a matching ticket. Metadata stays in
  // atomics so even a losing selector never races a later producer write.
  std::atomic<std::uint64_t> control{0};
  std::atomic<std::uint64_t> acceptedUs{0};
  std::atomic<std::uint64_t> identity{0};
};

struct Ticket {
  std::uint64_t acceptedUs = 0;
  std::uint32_t generation = 0;
  std::uint16_t correlation = 0;
  std::uint16_t action = 0;
};

std::array<TicketSlot, InputFrameLatencyTracker::Capacity> slots{};
std::atomic<std::uint32_t> reservationCursor{0};
std::atomic<std::uint32_t> nextCorrelation{0};
std::atomic<std::uint64_t> overflowCount{0};

constexpr std::uint16_t ActionMask = 0x000fU;
constexpr std::uint64_t StateMask = 0x7U;

constexpr std::uint64_t Control(std::uint64_t version,
                                TicketState state) noexcept {
  return (version << 3U) | static_cast<std::uint64_t>(state);
}

constexpr TicketState State(std::uint64_t control) noexcept {
  return static_cast<TicketState>(control & StateMask);
}

constexpr std::uint64_t Version(std::uint64_t control) noexcept {
  return control >> 3U;
}

constexpr std::uint64_t Identity(const Ticket &ticket) noexcept {
  return (static_cast<std::uint64_t>(ticket.generation) << 32U) |
         (static_cast<std::uint64_t>(ticket.correlation) << 16U) |
         ticket.action;
}

std::uint16_t NextCorrelation() noexcept {
  for (;;) {
    const std::uint16_t correlation = static_cast<std::uint16_t>(
        nextCorrelation.fetch_add(1U, std::memory_order_relaxed) + 1U);
    if (correlation != 0U) {
      return correlation;
    }
  }
}

Ticket ReadTicket(const TicketSlot &slot) noexcept {
  const std::uint64_t identity = slot.identity.load(std::memory_order_relaxed);
  return Ticket{
      slot.acceptedUs.load(std::memory_order_relaxed),
      static_cast<std::uint32_t>(identity >> 32U),
      static_cast<std::uint16_t>((identity >> 16U) & 0xffffU),
      static_cast<std::uint16_t>(identity & 0xffffU),
  };
}

void ReleaseSlot(TicketSlot &slot, std::uint64_t claimedControl) noexcept {
  slot.control.store(Control(Version(claimedControl) + 1U, TicketState::Empty),
                     std::memory_order_release);
}

void PublishDropped(const Ticket &ticket, std::uint64_t timestampUs,
                    TraceFlag reason, TraceThread thread) noexcept {
  const std::uint16_t flags = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(reason) | (ticket.action & ActionMask));
  Profiler::EmitAt(timestampUs, TraceCategory::Input,
                   TraceName::InputLatencyDropped, TracePhase::Instant,
                   ticket.correlation, thread, ticket.generation, flags);
}

void ExpireAt(std::uint64_t nowUs, std::uint32_t currentGeneration) noexcept {
  for (TicketSlot &slot : slots) {
    std::uint64_t control = slot.control.load(std::memory_order_acquire);
    if (State(control) != TicketState::Accepted &&
        State(control) != TicketState::Dispatched) {
      continue;
    }

    const Ticket ticket = ReadTicket(slot);
    const bool staleGeneration = ticket.generation != currentGeneration;
    const bool timedOut = nowUs >= ticket.acceptedUs &&
                          nowUs - ticket.acceptedUs >=
                              InputFrameLatencyTracker::NoPresentationTimeoutUs;
    if (!staleGeneration && !timedOut) {
      continue;
    }
    if (!slot.control.compare_exchange_strong(
            control, Control(Version(control), TicketState::Reading),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      continue;
    }

    if (!staleGeneration) {
      PublishDropped(ticket, nowUs, TraceFlag::InputNoPresentation,
                     TraceThread::Application);
    }
    ReleaseSlot(slot, control);
  }
}

std::uint32_t SaturatingLatency(std::uint64_t acceptedUs,
                                std::uint64_t presentedUs) noexcept {
  if (presentedUs <= acceptedUs) {
    return 0U;
  }
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      presentedUs - acceptedUs, std::numeric_limits<std::uint32_t>::max()));
}
} // namespace

std::uint16_t
InputFrameLatencyTracker::AcceptPress(std::uint16_t action) noexcept {
  if (!Profiler::CategoryEnabled(TraceCategory::Input)) {
    return 0U;
  }

  const std::uint32_t generation = Profiler::Generation();
  const std::uint64_t acceptedUs = Profiler::TimestampNow();
  ExpireAt(acceptedUs, generation);

  const Ticket ticket{acceptedUs, generation, NextCorrelation(), action};
  const std::uint32_t start =
      reservationCursor.fetch_add(1U, std::memory_order_relaxed);
  for (std::size_t offset = 0; offset < slots.size(); ++offset) {
    TicketSlot &slot = slots[(start + offset) % slots.size()];
    std::uint64_t control = slot.control.load(std::memory_order_acquire);
    if (State(control) != TicketState::Empty ||
        !slot.control.compare_exchange_strong(
            control, Control(Version(control), TicketState::Writing),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      continue;
    }
    slot.acceptedUs.store(ticket.acceptedUs, std::memory_order_relaxed);
    slot.identity.store(Identity(ticket), std::memory_order_relaxed);
    slot.control.store(Control(Version(control), TicketState::Accepted),
                       std::memory_order_release);
    Profiler::EmitAt(ticket.acceptedUs, TraceCategory::Input,
                     TraceName::InputAccepted, TracePhase::Instant,
                     ticket.correlation, TraceThread::Browser,
                     ticket.generation,
                     static_cast<std::uint16_t>(ticket.action & ActionMask));
    return ticket.correlation;
  }

  overflowCount.fetch_add(1U, std::memory_order_relaxed);
  // The browser transition itself was accepted even though tracing could not
  // retain another ticket. Publish both facts so instrumentation overload is
  // never mistaken for application latency.
  Profiler::EmitAt(ticket.acceptedUs, TraceCategory::Input,
                   TraceName::InputAccepted, TracePhase::Instant,
                   ticket.correlation, TraceThread::Browser, ticket.generation,
                   static_cast<std::uint16_t>(ticket.action & ActionMask));
  PublishDropped(ticket, ticket.acceptedUs, TraceFlag::InputOverflow,
                 TraceThread::Browser);
  return ticket.correlation;
}

void InputFrameLatencyTracker::CancelAccepted(
    std::uint16_t correlation) noexcept {
  if (correlation == 0U) {
    return;
  }
  for (TicketSlot &slot : slots) {
    std::uint64_t control = slot.control.load(std::memory_order_acquire);
    if (State(control) != TicketState::Accepted) {
      continue;
    }
    const Ticket ticket = ReadTicket(slot);
    if (ticket.correlation != correlation ||
        !slot.control.compare_exchange_strong(
            control, Control(Version(control), TicketState::Reading),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      continue;
    }
    const std::uint32_t generation = Profiler::Generation();
    if (ticket.generation == generation &&
        Profiler::CategoryEnabled(TraceCategory::Input)) {
      PublishDropped(ticket, Profiler::TimestampNow(),
                     TraceFlag::InputCoalesced, TraceThread::Browser);
    }
    ReleaseSlot(slot, control);
    return;
  }
}

void InputFrameLatencyTracker::MarkDispatching(std::uint16_t action,
                                               bool pressed) noexcept {
  if (!pressed || !Profiler::CategoryEnabled(TraceCategory::Input)) {
    return;
  }

  const std::uint32_t generation = Profiler::Generation();
  TicketSlot *selected = nullptr;
  Ticket selectedTicket{};
  std::uint64_t selectedControl = 0;
  for (TicketSlot &slot : slots) {
    const std::uint64_t control = slot.control.load(std::memory_order_acquire);
    if (State(control) != TicketState::Accepted) {
      continue;
    }
    const Ticket candidate = ReadTicket(slot);
    if (candidate.generation != generation || candidate.action != action) {
      continue;
    }
    if (selected == nullptr ||
        candidate.acceptedUs < selectedTicket.acceptedUs ||
        (candidate.acceptedUs == selectedTicket.acceptedUs &&
         candidate.correlation < selectedTicket.correlation)) {
      selected = &slot;
      selectedTicket = candidate;
      selectedControl = control;
    }
  }
  if (selected == nullptr) {
    return;
  }
  selected->control.compare_exchange_strong(
      selectedControl,
      Control(Version(selectedControl), TicketState::Dispatched),
      std::memory_order_acq_rel, std::memory_order_acquire);
}

void InputFrameLatencyTracker::PresentedFrame() noexcept {
  if (!Profiler::CategoryEnabled(TraceCategory::Input)) {
    return;
  }

  const std::uint32_t generation = Profiler::Generation();
  const std::uint64_t presentedUs = Profiler::TimestampNow();
  ExpireAt(presentedUs, generation);

  std::array<Ticket, Capacity> presented{};
  std::size_t count = 0;
  for (TicketSlot &slot : slots) {
    std::uint64_t control = slot.control.load(std::memory_order_acquire);
    if (State(control) != TicketState::Dispatched ||
        !slot.control.compare_exchange_strong(
            control, Control(Version(control), TicketState::Reading),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      continue;
    }
    const Ticket ticket = ReadTicket(slot);
    if (ticket.generation == generation) {
      presented[count++] = ticket;
    }
    ReleaseSlot(slot, control);
  }
  if (count == 0U) {
    return;
  }

  std::sort(presented.begin(), presented.begin() + count,
            [](const Ticket &left, const Ticket &right) {
              return left.acceptedUs < right.acceptedUs ||
                     (left.acceptedUs == right.acceptedUs &&
                      left.correlation < right.correlation);
            });
  for (std::size_t index = 0; index < count; ++index) {
    const Ticket &ticket = presented[index];
    Profiler::EmitAt(presentedUs, TraceCategory::Input,
                     TraceName::InputPresented, TracePhase::Instant,
                     ticket.correlation, TraceThread::Application,
                     ticket.generation,
                     static_cast<std::uint16_t>(ticket.action & ActionMask));
    // Counter flags are name-specific here: all 16 bits carry the same
    // correlation ID as the adjacent accepted/presented instants. Action bits
    // remain on those instants, so the two payloads cannot be confused.
    Profiler::EmitAt(
        presentedUs, TraceCategory::Input, TraceName::InputToFrameLatencyUs,
        TracePhase::Counter, SaturatingLatency(ticket.acceptedUs, presentedUs),
        TraceThread::Application, ticket.generation, ticket.correlation);
  }
}

void InputFrameLatencyTracker::ObserveNoPresentation() noexcept {
  if (!Profiler::CategoryEnabled(TraceCategory::Input)) {
    return;
  }
  const std::uint32_t generation = Profiler::Generation();
  ExpireAt(Profiler::TimestampNow(), generation);
}

#ifdef HOST_TEST
void InputFrameLatencyTracker::ResetForTesting() noexcept {
  for (TicketSlot &slot : slots) {
    slot.control.store(Control(0, TicketState::Empty),
                       std::memory_order_relaxed);
    slot.acceptedUs.store(0, std::memory_order_relaxed);
    slot.identity.store(0, std::memory_order_relaxed);
  }
  reservationCursor.store(0, std::memory_order_relaxed);
  nextCorrelation.store(0, std::memory_order_relaxed);
  overflowCount.store(0, std::memory_order_relaxed);
}

std::size_t InputFrameLatencyTracker::PendingForTesting() noexcept {
  std::size_t pending = 0;
  for (TicketSlot &slot : slots) {
    const TicketState state =
        State(slot.control.load(std::memory_order_acquire));
    if (state == TicketState::Accepted || state == TicketState::Dispatched) {
      ++pending;
    }
  }
  return pending;
}

std::uint64_t InputFrameLatencyTracker::OverflowForTesting() noexcept {
  return overflowCount.load(std::memory_order_relaxed);
}
#endif
