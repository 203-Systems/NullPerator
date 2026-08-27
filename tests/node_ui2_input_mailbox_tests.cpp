/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/node/ui2/NodeUi2InputMailbox.h"

#include "doctest/doctest.h"

namespace {

using node::ui2::InputMailbox;

std::uint16_t Mask(std::initializer_list<TrackerAction> actions) {
  std::uint16_t mask = 0U;
  for (const TrackerAction action : actions)
    mask |= TrackerActionBit(action);
  return mask;
}

void CheckEvent(const InputMailbox::Batch &batch, std::size_t index,
                TrackerAction action, bool pressed, std::uint8_t count = 1U,
                bool repeat = false) {
  REQUIRE(index < batch.size);
  CHECK(batch.events[index].action == action);
  CHECK(batch.events[index].pressed == pressed);
  CHECK(batch.events[index].count == count);
  CHECK(batch.events[index].repeat == repeat);
}

} // namespace

TEST_CASE("Node UI2 mailbox presses modifiers before directions") {
  InputMailbox mailbox;
  mailbox.PublishSample(
      Mask({TrackerAction::Right, TrackerAction::Option,
            TrackerAction::Shift, TrackerAction::Edit,
            TrackerAction::Play}),
      false, 100U);

  const InputMailbox::Batch batch = mailbox.Drain();
  REQUIRE(batch.size == 5U);
  CheckEvent(batch, 0U, TrackerAction::Shift, true);
  CheckEvent(batch, 1U, TrackerAction::Option, true);
  CheckEvent(batch, 2U, TrackerAction::Edit, true);
  CheckEvent(batch, 3U, TrackerAction::Right, true);
  CheckEvent(batch, 4U, TrackerAction::Play, true);
}

TEST_CASE("Node UI2 mailbox releases before replacement presses") {
  InputMailbox mailbox;
  mailbox.PublishSample(Mask({TrackerAction::Shift, TrackerAction::Left}),
                        false, 100U);
  (void)mailbox.Drain();

  mailbox.PublishSample(Mask({TrackerAction::Right}), false, 110U);
  // The global 5 ms kill defers the replacement press in the release sample.
  mailbox.PublishSample(Mask({TrackerAction::Right}), false, 120U);
  const InputMailbox::Batch batch = mailbox.Drain();

  REQUIRE(batch.size == 3U);
  CheckEvent(batch, 0U, TrackerAction::Left, false);
  CheckEvent(batch, 1U, TrackerAction::Shift, false);
  CheckEvent(batch, 2U, TrackerAction::Right, true);
}

TEST_CASE("Node UI2 mailbox retains release followed by repress") {
  InputMailbox mailbox;
  const std::uint16_t left = Mask({TrackerAction::Left});
  mailbox.PublishSample(left, false, 100U);
  (void)mailbox.Drain();

  mailbox.PublishSample(0U, false, 110U);
  mailbox.PublishSample(left, false, 120U);
  const InputMailbox::Batch batch = mailbox.Drain();

  REQUIRE(batch.size == 2U);
  CheckEvent(batch, 0U, TrackerAction::Left, false);
  CheckEvent(batch, 1U, TrackerAction::Left, true);
  CHECK(batch.heldMask == left);
}

TEST_CASE("Node UI2 mailbox reconstructs release before an unseen repress") {
  InputMailbox mailbox;
  const std::uint16_t play = Mask({TrackerAction::Play});
  mailbox.PublishSample(0U, false, 100U);
  (void)mailbox.Drain();

  mailbox.PublishSample(play, false, 110U);
  mailbox.PublishSample(0U, false, 120U);
  mailbox.PublishSample(play, false, 130U);
  const InputMailbox::Batch batch = mailbox.Drain();

  REQUIRE(batch.size == 3U);
  CheckEvent(batch, 0U, TrackerAction::Play, true);
  CheckEvent(batch, 1U, TrackerAction::Play, false);
  CheckEvent(batch, 2U, TrackerAction::Play, true);
  CHECK(batch.heldMask == play);
}

TEST_CASE("Node UI2 mailbox keeps a complete tap between drains") {
  InputMailbox mailbox;
  mailbox.PublishSample(0U, false, 100U);
  (void)mailbox.Drain();

  mailbox.PublishSample(Mask({TrackerAction::Play}), false, 110U);
  mailbox.PublishSample(0U, false, 120U);
  const InputMailbox::Batch batch = mailbox.Drain();

  REQUIRE(batch.size == 2U);
  CheckEvent(batch, 0U, TrackerAction::Play, true);
  CheckEvent(batch, 1U, TrackerAction::Play, false);
  CHECK(batch.heldMask == 0U);
}

TEST_CASE("Node UI2 mailbox orders completed modifier tap before direction") {
  InputMailbox mailbox;
  mailbox.PublishSample(0U, false, 100U);
  (void)mailbox.Drain();

  const std::uint16_t chord =
      Mask({TrackerAction::Shift, TrackerAction::Left});
  mailbox.PublishSample(chord, false, 110U);
  mailbox.PublishSample(0U, false, 120U);
  const InputMailbox::Batch batch = mailbox.Drain();

  REQUIRE(batch.size == 4U);
  CheckEvent(batch, 0U, TrackerAction::Shift, true);
  CheckEvent(batch, 1U, TrackerAction::Shift, false);
  CheckEvent(batch, 2U, TrackerAction::Left, true);
  CheckEvent(batch, 3U, TrackerAction::Left, false);
}

TEST_CASE("Node UI2 mailbox defers killed press but never release") {
  InputMailbox mailbox;
  mailbox.PublishSample(0U, false, 100U);
  (void)mailbox.Drain();

  const std::uint16_t edit = Mask({TrackerAction::Edit});
  mailbox.PublishSample(edit, false, 102U);
  CHECK(mailbox.Drain().size == 0U);

  mailbox.PublishSample(edit, false, 106U);
  InputMailbox::Batch batch = mailbox.Drain();
  REQUIRE(batch.size == 1U);
  CheckEvent(batch, 0U, TrackerAction::Edit, true);

  // The release is dispatched immediately even though it is only 1 ms after
  // the accepted press.
  mailbox.PublishSample(0U, false, 107U);
  batch = mailbox.Drain();
  REQUIRE(batch.size == 1U);
  CheckEvent(batch, 0U, TrackerAction::Edit, false);
}

TEST_CASE("Node UI2 direction repeat uses 500 ms delay and 75 ms period") {
  InputMailbox mailbox;
  const std::uint16_t down = Mask({TrackerAction::Down});
  mailbox.PublishSample(down, false, 100U);
  (void)mailbox.Drain();

  mailbox.PublishSample(down, false, 599U);
  CHECK(mailbox.Drain().size == 0U);

  mailbox.PublishSample(down, false, 750U);
  const InputMailbox::Batch batch = mailbox.Drain();
  REQUIRE(batch.size == 1U);
  CheckEvent(batch, 0U, TrackerAction::Down, true, 3U, true);
}

TEST_CASE("Node UI2 direction repeat debt saturates") {
  InputMailbox mailbox;
  const std::uint16_t up = Mask({TrackerAction::Up});
  mailbox.PublishSample(up, false, 10U);
  (void)mailbox.Drain();

  mailbox.PublishSample(up, false, 1'000'000U);
  const InputMailbox::Batch batch = mailbox.Drain();
  REQUIRE(batch.size == 1U);
  CheckEvent(batch, 0U, TrackerAction::Up, true,
             InputMailbox::kMaxRepeatDebt, true);
}

TEST_CASE("Node UI2 mailbox reports latest headphone route once") {
  InputMailbox mailbox;
  mailbox.PublishSample(0U, false, 10U);
  InputMailbox::Batch batch = mailbox.Drain();
  CHECK(batch.headphoneChanged);
  CHECK_FALSE(batch.headphoneConnected);

  mailbox.PublishSample(0U, false, 20U);
  batch = mailbox.Drain();
  CHECK_FALSE(batch.headphoneChanged);

  mailbox.PublishSample(0U, true, 30U);
  batch = mailbox.Drain();
  CHECK(batch.headphoneChanged);
  CHECK(batch.headphoneConnected);
}

TEST_CASE("Node UI2 mailbox ignores reserved input bits") {
  InputMailbox mailbox;
  const std::uint16_t reserved =
      TrackerActionBit(TrackerAction::Reserved8) |
      TrackerActionBit(TrackerAction::Reserved9);
  mailbox.PublishSample(reserved, false, 10U);
  const InputMailbox::Batch batch = mailbox.Drain();
  CHECK(batch.size == 0U);
  CHECK(batch.heldMask == 0U);
  CHECK(mailbox.LatestPhysicalHeldMask() == 0U);
}

TEST_CASE("Node UI2 mailbox worst coalesced chord fits fixed batch") {
  InputMailbox mailbox;
  const std::uint16_t all =
      Mask({TrackerAction::Left, TrackerAction::Down, TrackerAction::Right,
            TrackerAction::Up, TrackerAction::Shift, TrackerAction::Option,
            TrackerAction::Edit, TrackerAction::Play,
            TrackerAction::Power});
  mailbox.PublishSample(0U, false, 0U);
  (void)mailbox.Drain();

  mailbox.PublishSample(all, false, 10U);
  mailbox.PublishSample(0U, false, 20U);
  mailbox.PublishSample(all, false, 30U);
  mailbox.PublishSample(all, false, 1'000U);
  const InputMailbox::Batch batch = mailbox.Drain();

  // Nine press/release/repress sequences plus four aggregated repeat records.
  CHECK(batch.size == 31U);
  CHECK(batch.size <= InputMailbox::Batch::kCapacity);
  CHECK(batch.heldMask == all);
}
