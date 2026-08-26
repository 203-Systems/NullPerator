/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "HexBuffers.h"
#include "Application/Utils/FourCCSerialization.h"
#include "Application/Utils/char.h"
#include "Externals/etl/include/etl/string.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#define XML_CUT_LENGTH 64

void prepareHexChunk(tinyxml2::XMLPrinter *printer, unsigned char *datasrc,
                     int len) {

  bool singleValue = true;
  int singleValueData = -1;
  unsigned char hexBuffer[XML_CUT_LENGTH * 2 + 1] = "";

  char *hex = (char *)hexBuffer;
  for (int i = 0; i < len; i++) {
    hex2char(*datasrc, hex);
    if (singleValueData == -1) {
      singleValueData = *datasrc;
    } else {
      if (singleValueData != *datasrc) {
        singleValue = false;
      }
    };
    datasrc++;
    hex += 2;
  };
  if (singleValue) {
    printer->PushAttribute("VALUE", singleValueData);
    printer->PushAttribute("LENGTH", len);
  } else {
    printer->PushText(reinterpret_cast<const char *>(hexBuffer));
  }
}

void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   unsigned char *src, unsigned len) {

  printer->OpenElement(nodeName);

  unsigned int count = len / XML_CUT_LENGTH;
  unsigned char *datasrc = (unsigned char *)src;

  for (unsigned i = 0; i < count; i++) {
    printer->OpenElement("DATA");
    prepareHexChunk(printer, datasrc, XML_CUT_LENGTH);
    datasrc += XML_CUT_LENGTH;
    printer->CloseElement();
  };

  len -= count * XML_CUT_LENGTH;
  if (len > 0) {
    printer->OpenElement("DATA");
    prepareHexChunk(printer, datasrc, len);
    printer->CloseElement();
  }
  printer->CloseElement();
};

void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   unsigned int *src, unsigned len) {
  saveHexBuffer(printer, nodeName, (unsigned char *)src, len * sizeof(int));
}

void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   unsigned short *src, unsigned len) {
  saveHexBuffer(printer, nodeName, (unsigned char *)src, len * sizeof(short));
}

void saveHexBuffer(tinyxml2::XMLPrinter *printer, const char *nodeName,
                   FourCC *src, unsigned len) {
  // Persist command IDs in canonical one-byte chunks without allocating a
  // second phrase-sized buffer on constrained targets.
  printer->OpenElement(nodeName);
  std::array<unsigned char, XML_CUT_LENGTH> packed{};
  for (unsigned offset = 0; offset < len; offset += XML_CUT_LENGTH) {
    const unsigned chunk = std::min<unsigned>(XML_CUT_LENGTH, len - offset);
    for (unsigned index = 0; index < chunk; ++index) {
      packed[index] =
          static_cast<unsigned char>(src[offset + index].get_value());
    }
    printer->OpenElement("DATA");
    prepareHexChunk(printer, packed.data(), static_cast<int>(chunk));
    printer->CloseElement();
  }
  printer->CloseElement();
}

namespace {

bool ParseBoundedDecimal(const char *text, std::size_t maximum,
                         std::size_t &result) {
  if (text == nullptr || text[0] == '\0')
    return false;

  std::size_t value = 0U;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9')
      return false;
    const std::size_t digit = static_cast<std::size_t>(*cursor - '0');
    if (digit > maximum || value > (maximum - digit) / 10U)
      return false;
    value = value * 10U + digit;
  }
  result = value;
  return true;
}

// FourCC command streams have historically been persisted using several
// platform widths.  An old RLE run may therefore describe more bytes than the
// current destination can consume.  Validate the entire decimal token, but
// saturate at the migration decoder's fixed byte budget so hostile lengths do
// not turn into either an overflow or an unbounded loop.
bool ParseClampedDecimal(const char *text, std::size_t maximum,
                         std::size_t &result) {
  if (text == nullptr || text[0] == '\0')
    return false;

  std::size_t value = 0U;
  bool clamped = false;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9')
      return false;
    if (clamped)
      continue;
    const std::size_t digit = static_cast<std::size_t>(*cursor - '0');
    if (value > maximum / 10U ||
        (value == maximum / 10U && digit > maximum % 10U)) {
      value = maximum;
      clamped = true;
    } else {
      value = value * 10U + digit;
    }
  }
  result = value;
  return true;
}

int HexNibble(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  return -1;
}

bool RestoreError(PersistencyDocument *doc) {
  if (doc != nullptr)
    doc->MarkError();
  return false;
}

} // namespace

bool restoreHexBuffer(PersistencyDocument *doc, unsigned char *destination,
                      std::size_t destinationCapacity) {
  if (doc == nullptr || (destination == nullptr && destinationCapacity != 0U))
    return RestoreError(doc);

  std::size_t written = 0U;

  bool child = doc->FirstChild();
  while (child) {
    bool hasAttr = doc->NextAttribute();
    if (hasAttr) {
      std::size_t data = 0U;
      std::size_t length = 0U;
      bool gotData = false;
      bool gotLength = false;
      while (hasAttr) {
        if (!strcmp(doc->attrname_, "VALUE")) {
          if (!ParseBoundedDecimal(doc->attrval_, 0xFFU, data))
            return RestoreError(doc);
          gotData = true;
        }
        if (!strcmp(doc->attrname_, "LENGTH")) {
          if (!ParseBoundedDecimal(doc->attrval_, destinationCapacity,
                                   length))
            return RestoreError(doc);
          gotLength = true;
        }
        hasAttr = doc->NextAttribute();
      }
      if (doc->HadError() || !gotData || !gotLength ||
          length > destinationCapacity - written)
        return RestoreError(doc);
      if (length != 0U)
        memset(destination + written, static_cast<int>(data), length);
      written += length;
    } else {
      if (doc->HadError())
        return false;
      if (doc->HasContent()) {
        if (doc->HadError())
          return false;
        const std::size_t chars = strlen(doc->content_);
        if ((chars & 1U) != 0U)
          return RestoreError(doc);
        const std::size_t bytes = chars / 2U;
        if (bytes > destinationCapacity - written)
          return RestoreError(doc);
        for (std::size_t i = 0; i < bytes; ++i) {
          const int high = HexNibble(doc->content_[i * 2U]);
          const int low = HexNibble(doc->content_[i * 2U + 1U]);
          if (high < 0 || low < 0)
            return RestoreError(doc);
          destination[written++] =
              static_cast<unsigned char>((high << 4U) | low);
        }
      }
    }
    child = doc->NextSibling();
  }
  return !doc->HadError();
}

bool restoreHexBuffer(PersistencyDocument *doc, FourCC *destination,
                      unsigned len) {
  if (doc == nullptr || (destination == nullptr && len != 0U))
    return RestoreError(doc);
  if (static_cast<std::size_t>(len) >
      std::numeric_limits<std::size_t>::max() / 4U)
    return RestoreError(doc);
  const std::size_t encodedCapacity = static_cast<std::size_t>(len) * 4U;
  std::size_t encodedBytes = 0U;
  FourCCSerialization::CommandStreamDecoder decoder(destination, len);

  bool child = doc->FirstChild();
  while (child) {
    bool hasAttr = doc->NextAttribute();
    if (hasAttr) {
      std::size_t data = 0U;
      std::size_t length = 0U;
      bool gotData = false;
      bool gotLength = false;
      while (hasAttr) {
        if (!strcmp(doc->attrname_, "VALUE")) {
          if (gotData || !ParseBoundedDecimal(doc->attrval_, 0xFFU, data))
            return RestoreError(doc);
          gotData = true;
        }
        if (!strcmp(doc->attrname_, "LENGTH")) {
          if (gotLength ||
              !ParseClampedDecimal(doc->attrval_,
                                   encodedCapacity - encodedBytes, length))
            return RestoreError(doc);
          gotLength = true;
        }
        hasAttr = doc->NextAttribute();
      }
      if (doc->HadError() || !gotData || !gotLength || length == 0U ||
          length > encodedCapacity - encodedBytes)
        return RestoreError(doc);
      decoder.PushRepeated(static_cast<std::uint8_t>(data), length);
      encodedBytes += length;
    } else {
      if (doc->HadError())
        return false;
      if (!doc->HasContent()) {
        child = doc->NextSibling();
        continue;
      }
      const std::size_t chars = strlen(doc->content_);
      if ((chars & 1U) != 0U)
        return RestoreError(doc);
      const std::size_t bytes = chars / 2U;
      if (bytes > encodedCapacity - encodedBytes)
        return RestoreError(doc);
      for (std::size_t index = 0; index < bytes; ++index) {
        const int high = HexNibble(doc->content_[index * 2U]);
        const int low = HexNibble(doc->content_[index * 2U + 1U]);
        if (high < 0 || low < 0)
          return RestoreError(doc);
        decoder.Push(static_cast<std::uint8_t>((high << 4U) | low));
      }
      encodedBytes += bytes;
    }
    child = doc->NextSibling();
  }
  decoder.Finalize();
  return !doc->HadError();
}
