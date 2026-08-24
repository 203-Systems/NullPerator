#pragma once

#include <cstdint>

#define SONG_CHANNEL_COUNT 8

using stereosample = std::uint32_t;

class Player;
enum PlayerEventType { PET_START, PET_UPDATE, PET_STOP };
