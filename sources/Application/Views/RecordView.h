/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _RECORD_VIEW_H_
#define _RECORD_VIEW_H_

#include "Application/Model/Config.h"
#include "Application/Views/BaseClasses/View.h"
#include "BaseClasses/UIIntVarField.h"
#include "FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"
#include <array>
#include <cstdint>

enum class RecordViewUi2Focus : std::uint8_t {
  Source,
  LineGain,
  MicGain,
  Unknown,
};

enum class RecordViewUi2State : std::uint8_t {
  Idle,
  Recording,
  Saving,
};

struct RecordViewUi2Snapshot {
  std::array<char, 17> source{};
  std::array<char, 9> lineGain{};
  std::array<char, 9> micGain{};
  std::array<char, 6> elapsed{};
  RecordViewUi2Focus focus = RecordViewUi2Focus::Unknown;
  RecordViewUi2State state = RecordViewUi2State::Idle;
  std::uint8_t sourceIndex = 0;
  std::int8_t lineGainDb = 0;
  std::int8_t micGainDb = 0;
  std::uint8_t savingPercent = 0;
  // The current audio adapters expose no input meter samples. UI2 must draw a
  // neutral track until a platform supplies real levels rather than rendering
  // the approved fixture's synthetic meter as live data.
  bool meterAvailable = false;
  bool recordingAvailable = false;
};

class RecordView : public FieldView, public I_Observer {
public:
  RecordView(GUIWindow &w, ViewData *data);
  virtual ~RecordView();
  void Reset();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus();
  void OnFocusLost() override;

  [[nodiscard]] RecordViewUi2Snapshot SnapshotForUi2() const;

  // Observer for field changes
  void Update(Observable &, I_ObservableData *);

  void AnimationUpdate() override;

  // Static method to set which view will open the RecordView
  static void SetSourceViewType(ViewType vt);

  // Track which view opened the RecordView (defaults to song view)
  static ViewType sourceViewType_;

protected:
private:
  // UI fields
  etl::vector<UIIntVarField, 3> intVarField_;

  // Recording state
  bool uiRecordingActive_;
  bool uiSavingActive_;
  bool autoSwitchPending_;
  uint32_t recordingStartTime_;
  uint32_t recordingDuration_;

  // Helper methods
  void record();
  void stop();
  void stopAndSwitchToEditor();
  void updateTimeDisplay();
  void generateFullPath(etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename,
                        etl::string<MAX_PROJECT_SAMPLE_PATH_LENGTH> *fullpath);

  // Time display helpers
  void formatTime(uint32_t milliseconds, char *buffer, size_t bufferSize);

  void updateRecordingSource();
};

#endif
