/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Application/UI2/Controllers/Ui2SampleBrowserController.h"
#include "Application/UI2/Controllers/Ui2SampleEditorController.h"
#include "Application/UI2/Controllers/Ui2SampleSlicesController.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"
#include "Application/UI2/Ui2SampleEditorTransaction.h"
#include "Application/UI2/Ui2SampleWaveformBackend.h"

namespace ui2 {
// One owner for resources that must not survive an editor/project boundary.
// Controllers borrow waveform by reference, so this object cannot be moved.
class Ui2SampleWorkflow final {
public:
  enum class PreviewKind : std::uint8_t { None, EditorStream, SliceNote };
  struct Preview {
    PreviewKind kind = PreviewKind::None;
    std::uint32_t startedMs = 0, start = 0, end = 0, frames = 0, rate = 0;
    std::uint8_t instrument = 0, note = 0;
    bool singleCycle = false;
  };
  using StopTransport = void (*)(PreviewKind, std::uint8_t);
  explicit Ui2SampleWorkflow(StopTransport stop) : stop_(stop) {}
  Ui2SampleWorkflow(const Ui2SampleWorkflow &) = delete;
  Ui2SampleWorkflow &operator=(const Ui2SampleWorkflow &) = delete;
  void StopPreview() {
    if (stop_ && preview.kind != PreviewKind::None)
      stop_(preview.kind, preview.instrument);
    preview = {};
    editor.StopPreview();
    slices.StopPreview();
  }
  void Reset() {
    StopPreview();
    if (transaction.Active())
      (void)transaction.Discard();
    transaction.Reset();
    editor.Close();
    slices.Close();
    waveform.Reset();
    browser.Close();
    returnPage = UiApplicationPage::Instrument;
  }
  Ui2SampleBrowserController browser{};
  Ui2SampleWaveformBackend waveform{};
  Ui2SampleEditorController editor{waveform};
  Ui2SampleEditorTransaction transaction{};
  Ui2SampleSlicesController slices{waveform};
  Preview preview{};
  UiApplicationPage returnPage = UiApplicationPage::Instrument;

private:
  StopTransport stop_;
};
} // namespace ui2
