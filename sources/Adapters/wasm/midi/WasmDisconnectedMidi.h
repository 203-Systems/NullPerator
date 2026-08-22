/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_DISCONNECTED_MIDI_H
#define PICOTRACKER_WASM_DISCONNECTED_MIDI_H

#include "Services/Midi/MidiService.h"

class WasmDisconnectedMidi final : public MidiService {
public:
  WasmDisconnectedMidi();
};

#endif
