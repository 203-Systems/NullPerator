/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "Application/Persistency/Persistent.h"
#include "Foundation/T_Singleton.h"
#include "Foundation/Variables/StringVariable.h"
#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "System/Console/Trace.h"

#include <array>
#include <cstddef>
#include <cstdint>

class Config : public T_Singleton<Config>, public VariableContainer {
public:
  static constexpr std::size_t SemanticThemeColorCount = 19U;
  using SemanticThemeColors =
      std::array<std::uint32_t, SemanticThemeColorCount>;

  Config();
  ~Config();
  int GetValue(const char *key);
  void ProcessArguments(int argc, char **argv);
  bool Save();

  // Methods for handling color variables and themes
  void WriteColorVariables(tinyxml2::XMLPrinter *printer);
  void ReadColorVariable(PersistencyDocument *doc);

  // Theme-related methods (replacing Theme class)
  bool SaveTheme(tinyxml2::XMLPrinter *printer, const char *themeName);
  bool LoadTheme(PersistencyDocument *doc);
  bool ExportTheme(const char *themeName, bool overwrite);
  // `loaded` distinguishes a parsed/applied theme whose config sync failed
  // from a file that could not be loaded at all. Existing callers can ignore
  // the detail and retain the historical boolean success contract.
  bool ImportTheme(const char *themeName, bool *loaded = nullptr);

  // UI2 persists all nineteen public semantic roles independently. Legacy
  // FourCC colors remain source-only UI1 reference data and never seed UI2.
  [[nodiscard]] const SemanticThemeColors &GetSemanticThemeColors() const {
    return semanticThemeColors_;
  }
  void SetSemanticThemeColors(const SemanticThemeColors &colors) {
    semanticThemeColors_ = colors;
  }
  void SetSemanticThemeColor(std::size_t index, std::uint32_t color) {
    if (index < semanticThemeColors_.size())
      semanticThemeColors_[index] = color & 0x00FFFFFFU;
  }
  void ResetSemanticThemeColors();
  [[nodiscard]] static constexpr SemanticThemeColors
  DefaultSemanticThemeColors() {
    return {{0x030707U, 0x081210U, 0x081210U, 0xE8EEEBU, 0x596462U,
             0x041011U, 0x45DCE8U, 0x45DCE8U, 0x15181AU, 0x68E69AU,
             0x00DC74U, 0xF0CE00U, 0xF02E75U, 0xE8EEEBU, 0x00DC74U,
             0xF02E75U, 0x00DC74U, 0xF0CE00U, 0xF02E75U}};
  }

private:
  etl::list<Variable *, 26> variables_;
  // Config variables (kept as members to avoid heap allocation)
  WatchedVariable background_;
  WatchedVariable foreground_;
  WatchedVariable hiColor1_;
  WatchedVariable hiColor2_;
  WatchedVariable consoleColor_;
  WatchedVariable cursorColor_;
  WatchedVariable infoColor_;
  WatchedVariable warnColor_;
  WatchedVariable errorColor_;
  WatchedVariable accentColor_;
  WatchedVariable accentAltColor_;
  WatchedVariable emphasisColor_;
  WatchedVariable lineOut_;
  WatchedVariable midiDevice_;
  WatchedVariable midiSync_;
  WatchedVariable importResampler_;
  WatchedVariable uiFont_;
  WatchedVariable uiTextCase_;
  StringVariable<MAX_VARIABLE_STRING_LENGTH> themeName_;
  WatchedVariable backlightLevel_;
  WatchedVariable outputVolume_;
  WatchedVariable recordSource_;
  WatchedVariable recordLineGain_;
  WatchedVariable recordMicGain_;
  SemanticThemeColors semanticThemeColors_ = DefaultSemanticThemeColors();

  void SaveContent(tinyxml2::XMLPrinter *printer);
  void WriteSemanticThemeColors(tinyxml2::XMLPrinter *printer);
  [[nodiscard]] std::uint32_t
  ReadSemanticThemeColor(PersistencyDocument *doc);
  void useDefaultConfig();
};

#endif
