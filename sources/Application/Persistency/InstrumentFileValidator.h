/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _INSTRUMENT_FILE_VALIDATOR_H_
#define _INSTRUMENT_FILE_VALIDATOR_H_

// Accepts older instrument files that omit newer parameters, while requiring
// a complete INSTRUMENT envelope, at least one well-formed PARAM payload and
// semantically valid table references. The check is read-only and uses
// PersistencyDocument's fixed parser storage.
bool ValidateInstrumentFilePayload(const char *name);

#endif
