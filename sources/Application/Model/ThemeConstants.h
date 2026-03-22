/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _THEME_CONSTANTS_H_
#define _THEME_CONSTANTS_H_

// Define default color values to be used across the application
namespace ThemeConstants {
// Color constants
const uint32_t DEFAULT_BACKGROUND = 0x000000;
const uint32_t DEFAULT_FOREGROUND = 0xFFFFFF;
const uint32_t DEFAULT_HICOLOR1 = 0xBCBCBA;
const uint32_t DEFAULT_HICOLOR2 = 0x32ECFF;
const uint32_t DEFAULT_CONSOLECOLOR = 0x000000;
const uint32_t DEFAULT_CURSORCOLOR = 0x32ECFF;
const uint32_t DEFAULT_INFOCOLOR = 0x00FF50;
const uint32_t DEFAULT_WARNCOLOR = 0xFFE000;
const uint32_t DEFAULT_ERRORCOLOR = 0xFF3070;
const uint32_t DEFAULT_ACCENT = 0xFFFFFF;
const uint32_t DEFAULT_ACCENT_ALT = 0x787878;
const uint32_t DEFAULT_EMPHASIS = 0x787878;
// const uint32_t DEFAULT_RESERVED1 = 0x0000FF;
// const uint32_t DEFAULT_RESERVED2 = 0x555555;
// const uint32_t DEFAULT_RESERVED3 = 0x777777;
// const uint32_t DEFAULT_RESERVED4 = 0xFFFF00;

// Font constants
const int DEFAULT_UIFONT = 0x0;
const int FONT_COUNT = 3;
inline const char *FONT_NAMES[FONT_COUNT] = {"Regular", "Bold", "Wide"};

inline const char *DEFAULT_THEME_NAME = "Default";
} // namespace ThemeConstants

#endif // _THEME_CONSTANTS_H_
