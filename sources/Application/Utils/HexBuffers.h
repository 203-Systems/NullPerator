/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _HEX_BUFFERS_H_
#define _HEX_BUFFERS_H_

#include "Application/Persistency/PersistencyDocument.h"
#include "Externals/TinyXML2/tinyxml2.h"
#include "Foundation/Types/Types.h"

#include <cstddef>

void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   unsigned char *src, unsigned len);
void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   unsigned short *src, unsigned len);
void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   unsigned int *src, unsigned len);
void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   FourCC *src, unsigned len);
[[nodiscard]] bool restoreHexBuffer(PersistencyDocument *doc,
                                    unsigned char *dst,
                                    std::size_t destinationCapacity);
[[nodiscard]] bool restoreHexBuffer(PersistencyDocument *doc, FourCC *dst,
                                    unsigned len);

#endif
