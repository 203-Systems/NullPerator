/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_CORE_LINK_CLOSURE_H
#define PICOTRACKER_WASM_CORE_LINK_CLOSURE_H

// Each exported anchor returns the address of a typed C++ function-pointer
// object. The relocation retains the corresponding core object without
// invoking it, while the C names remain stable in optimized browser builds.
extern "C" {
const void *PicoTracker_Wasm_CoreApplicationAnchor();
const void *PicoTracker_Wasm_CoreAppWindowAnchor();
const void *PicoTracker_Wasm_CorePlayerAnchor();
const void *PicoTracker_Wasm_CoreViewAnchor();
}

#endif
