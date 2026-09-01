/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Foundation/Variables/Variable.h"

#include <array>
#include <cstddef>
#include <cstdint>

class PersistencyDocument;

inline constexpr std::size_t ProjectParameterRestoreCapacity = 16U;

struct ProjectParameterUpdate {
  Variable *target = nullptr;
  std::array<char, MAX_VARIABLE_STRING_LENGTH + 1U> value{};
};

struct ProjectParameterRestorePacket {
  std::array<ProjectParameterUpdate, ProjectParameterRestoreCapacity> updates{};
  std::uint8_t count = 0U;
};

using ProjectParameterResolver = Variable *(*)(void *context,
                                                const char *name);

// Parses bounded legacy PicoTracker versions such as "2.3-Beta3" into
// hundredths without floating conversion or exponent/overflow ambiguity.
[[nodiscard]] bool ParseProjectVersionHundredthsForRestore(const char *text,
                                                           int &result);

// Parses and validates the complete PARAMETER payload without mutating any
// Variable. The caller may commit the fixed packet only after this succeeds,
// preventing a later malformed element from leaving a partially restored
// Project behind.
[[nodiscard]] bool StageProjectParameterRestore(
    PersistencyDocument *document, void *resolverContext,
    ProjectParameterResolver resolver, ProjectParameterRestorePacket &packet);

// Checks Project-specific value domains without touching live Variables. The
// caller supplies the tempo contract because those bounds are owned by
// Project.h; all other bounds are intrinsic to their persisted FourCC fields.
[[nodiscard]] bool ValidateProjectParameterRestorePacket(
    const ProjectParameterRestorePacket &packet, int minimumTempo,
    int maximumTempo);

static_assert(sizeof(ProjectParameterRestorePacket) <= 1'024U,
              "project restore staging must remain embedded-friendly");
