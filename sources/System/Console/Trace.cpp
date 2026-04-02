/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Trace.h"
#include "Externals/etl/include/etl/error_handler.h"
#include "System/System/System.h"
#include <string.h>

// be explicit about the nanoprintf configuration
#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#include "nanoprintf.h"

Trace::Trace() {}

namespace {
bool LooksLikeErrorCategory(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  for (const char *c = text; *c != '\0'; ++c) {
    const bool is_upper = (*c >= 'A') && (*c <= 'Z');
    const bool is_digit = (*c >= '0') && (*c <= '9');
    const bool is_symbol = (*c == '_') || (*c == '-') || (*c == '*');
    if (!(is_upper || is_digit || is_symbol)) {
      return false;
    }
  }
  return true;
}
} // namespace

void Trace::trace_uart_putc(int c, void *context) {
  System *sys = System::GetInstance();
  sys->SystemPutChar(c);
}

//------------------------------------------------------------------------------

void Trace::VLog(const char *category, const char *fmt, va_list &args) {
#ifndef NDEBUG
  // first prepend the category
  npf_pprintf(&trace_uart_putc, NULL, "[%s] ", category);

  npf_vpprintf(&trace_uart_putc, NULL, fmt, args);
  // end with NL+CR as thats how it previously worked using stdio' printf
  npf_pprintf(&trace_uart_putc, NULL, "\r\n");
#else
  (void)category;
  (void)fmt;
  (void)args;
#endif
}

//------------------------------------------------------------------------------

void Trace::VError(const char *category, const char *fmt, va_list &args) {
#ifndef NDEBUG
  npf_pprintf(&trace_uart_putc, NULL, "[*ERROR*] ");
  if (category && category[0] != '\0') {
    npf_pprintf(&trace_uart_putc, NULL, "[%s] ", category);
  }
  npf_vpprintf(&trace_uart_putc, NULL, fmt, args);
  npf_pprintf(&trace_uart_putc, NULL, "\r\n");
#else
  (void)category;
  (void)fmt;
  (void)args;
#endif
}

//------------------------------------------------------------------------------

void Trace::Log(const char *category, const char *fmt, ...) {
#ifndef NDEBUG
  va_list args;
  va_start(args, fmt);
  VLog(category, fmt, args);
  va_end(args);
#else
  (void)category;
  (void)fmt;
#endif
}

//------------------------------------------------------------------------------

void Trace::Debug(const char *fmt, ...) {
#ifndef NDEBUG
  va_list args;
  va_start(args, fmt);
  VLog("-D-", fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

//------------------------------------------------------------------------------

void Trace::Error(const char *fmt, ...) {
#ifndef NDEBUG
  va_list args;
  va_start(args, fmt);

  if (LooksLikeErrorCategory(fmt)) {
    va_list tagged_args;
    va_copy(tagged_args, args);
    const char *tagged_fmt = va_arg(tagged_args, const char *);
    if (tagged_fmt != nullptr) {
      VError(fmt, tagged_fmt, tagged_args);
      va_end(tagged_args);
      va_end(args);
      return;
    }
    va_end(tagged_args);
  }

  VError(nullptr, fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

//------------------------------------------------------------------------------

// Never inline, so you can breakpoint on this function to get backtraces
__attribute__((noinline)) void EtlError(const etl::exception &e) {
  Trace::Error("ETL: %s:%d: %s", e.file_name(), e.line_number(), e.what());
}

void Trace::RegisterEtlErrorHandler() {
#ifdef ETL_LOG_ERRORS
  etl::error_handler::set_callback<EtlError>();
#endif
}

//------------------------------------------------------------------------------
