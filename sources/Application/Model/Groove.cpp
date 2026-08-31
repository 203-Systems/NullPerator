/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Groove.h"

#include <array>
#include <cstring>

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "groove telemetry requires lock-free 32-bit atomics");

unsigned char Groove::data_[MAX_GROOVES][16];

Groove::Groove() : Persistent("GROOVES") { Clear(); };

Groove::~Groove(){};

void Groove::Clear() {
  // Init all grooves with basic datas
  memset(data_, NO_GROOVE_DATA, sizeof(data_));
  for (int i = 0; i < MAX_GROOVES; i++) {
    data_[i][0] = 6;
    data_[i][1] = 6;
  };
  // init grooves selectah
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    // Channel
    channelGroove_[i].groove_ = 0;
    channelGroove_[i].position_ = 0;
    channelGroove_[i].ticks_ = data_[0][0];
    PublishChannelTelemetry(i);
  };
};
// Resest groove data at song startup

void Groove::Reset() {
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    // Channel
    ChannelGroove &c = channelGroove_[i];
    c.position_ = 0;
    c.ticks_ = data_[c.groove_][c.position_];
    PublishChannelTelemetry(i);
  }
};

void Groove::GetChannelData(int channel, int *groove, int *position) {
  const std::uint32_t telemetry =
      channelTelemetry_[channel].load(std::memory_order_relaxed);
  *groove = static_cast<int>(telemetry & 0xFFU);
  *position = static_cast<int>((telemetry >> 8U) & 0xFFU);
};

void Groove::PublishChannelTelemetry(int channel) {
  const ChannelGroove &groove = channelGroove_[channel];
  const std::uint32_t telemetry =
      static_cast<std::uint32_t>(groove.groove_) |
      (static_cast<std::uint32_t>(groove.position_) << 8U);
  channelTelemetry_[channel].store(telemetry, std::memory_order_relaxed);
}

void Groove::SaveContent(tinyxml2::XMLPrinter *printer) {
  saveHexBuffer(printer, "DATA", (unsigned char *)data_, 16 * MAX_GROOVES);
};

void Groove::RestoreContent(PersistencyDocument *doc) {
  if (doc->FirstChild()) {
    // Restore into a staging buffer so a malformed payload cannot leave a
    // partially updated groove table behind. Existing projects may omit the
    // unused tail, so preserve the current values before overlaying the file.
    std::array<unsigned char, sizeof(data_)> staged{};
    std::memcpy(staged.data(), data_, sizeof(data_));
    if (!restoreHexBuffer(doc, staged.data(), staged.size()))
      return;
    for (std::size_t groove = 0U; groove < MAX_GROOVES; ++groove) {
      bool hasTiming = false;
      for (std::size_t step = 0U; step < 16U; ++step) {
        unsigned char &ticks = staged[groove * 16U + step];
        if (ticks == NO_GROOVE_DATA)
          continue;
        if (ticks == 0U) {
          // PicoTracker 2.0-RC3 project files wrote zeroes into the unused
          // tail of some otherwise valid groove rows. Treat that legacy zero
          // as the modern 0xFF terminator. A zero in the first step still has
          // no valid timing context and remains a malformed payload.
          if (!hasTiming) {
            doc->MarkError();
            return;
          }
          ticks = NO_GROOVE_DATA;
          continue;
        }
        // A real step is 1..15; larger values are outside the editor/model
        // contract and would make playback timing undefined.
        if (ticks > 15U) {
          doc->MarkError();
          return;
        }
        hasTiming = true;
      }
    }
    std::memcpy(data_, staged.data(), sizeof(data_));

    // restoreHexBuffer consumed the nested DATA wrapper. Consume the closing
    // GROOVES element as well so PersistencyService can advance to the next
    // registered model node instead of mistaking this close for end-of-root.
    if (doc->NextSibling()) {
      doc->MarkError();
      return;
    }
  }
}

// Trigger grooves so we go to the next step
void Groove::Trigger() {
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    ChannelGroove &c = channelGroove_[i];
    if (UpdateGroove(c, false))
      PublishChannelTelemetry(i);
  }
};

bool Groove::UpdateGroove(ChannelGroove &c, bool reverse) {

  bool stepped = false;

  if (reverse) { // Table
    c.ticks_++;
    if (c.groove_ == 255) { // Default table groove
      if (c.ticks_ == 1) {
        stepped = true;
        c.ticks_ = 0;
      }
    } else {
      if (c.ticks_ == data_[c.groove_][c.position_]) {
        c.position_ = (c.position_ + 1) % 16;
        if (data_[c.groove_][c.position_] == 0xFF) {
          c.position_ = 0;
        };
        c.ticks_ = 0;
        stepped = true;
      }
    }
  } else { // Note
    if (c.ticks_ == 0) {
      c.position_ = (c.position_ + 1) % 16;
      if (data_[c.groove_][c.position_] == 0xFF) {
        c.position_ = 0;
      };
      c.ticks_ = data_[c.groove_][c.position_];
      stepped = true;
    };
    c.ticks_--;
  }
  return stepped;
}

void Groove::SetGroove(int channel, int groove) {
  if (groove >= MAX_GROOVES)
    return;
  channelGroove_[channel].groove_ = groove;
  channelGroove_[channel].position_ = 0;
  channelGroove_[channel].ticks_ =
      data_[channelGroove_[channel].groove_][channelGroove_[channel].position_];
  PublishChannelTelemetry(channel);
};

// Returns true if, according to current groove setting it is time to go
// to the next sequencing step

bool Groove::TriggerChannel(int i) {
  ChannelGroove &c = channelGroove_[i];
  return ((c.ticks_) % (data_[c.groove_][c.position_]) == 0);
};

unsigned char *Groove::GetGrooveData(int groove) { return data_[groove]; };
