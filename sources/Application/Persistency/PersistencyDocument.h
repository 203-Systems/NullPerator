/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PERSISTENCY_DOCUMENT_H_
#define _PERSISTENCY_DOCUMENT_H_

#include "Externals/yxml/yxml.h"
#include "System/FileSystem/FileHandle.h"
#include "System/FileSystem/FileSystem.h"

class PersistencyDocument {
public:
  PersistencyDocument();
  ~PersistencyDocument(); // Add destructor
  bool Load(const char *filename);
  void Close(); // Add method to explicitly close the file

  // r_ < YXML_OK to signal that the xml parsing had a fatal error
  bool HadError() const { return r_ < YXML_OK; }

  bool FirstChild();
  bool NextSibling();
  bool NextAttribute();
  bool HasContent();
  // Consume the remainder of the stream and ask yxml to validate that every
  // opened element was closed.  Streaming restore code otherwise cannot tell
  // a clean closing tag from a truncated file that merely reached EOF.
  bool Finish();
  // Restore helpers use the parser error channel for semantic payload errors
  // too. This keeps the legacy void RestoreContent interface while ensuring a
  // malformed binary field makes the enclosing project load fail.
  void MarkError() { r_ = YXML_ESYN; }
  char *ElemName();

  char attrname_[64];
  char attrval_[64];
  char content_[129]; // 128 + \0
  yxml_ret_t r_;

  int version_;

private:
  inline static char stack_[1024];
  inline static yxml_t state_[1];
  FileHandle fp_;
};
#endif
