#include "doctest/doctest.h"

#include "Application/Views/ModalDialogs/RenameModalView.h"
#include "Application/Views/ModalDialogs/MessageBox.h"

#include "System/System/System.h"
#include "UIFramework/Interfaces/I_GUIWindowImp.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

namespace {

class TestWindowImp final : public I_GUIWindowImp {
public:
  void Clear(GUIColor &, bool) override {}
  void SetColor(GUIColor &) override {}
  void ClearTextRect(GUIRect &) override {}
  void DrawString(const char *, const GUIPoint &, const GUITextProperties &,
                  bool) override {}
  void DrawChar(char, const GUIPoint &, const GUITextProperties &) override {}
  GUIRect GetRect() override { return {0, 0, 240, 240}; }
  void Invalidate() override {}
  void Lock() override {}
  void Unlock() override {}
  void Flush() override {}
  void PushEvent(GUIEvent &) override {}
  void DrawRect(GUIRect &) override {}
};

class TestWindow final : public GUIWindow {
public:
  explicit TestWindow(I_GUIWindowImp &imp) : GUIWindow(imp) {}
  void onUpdate(bool) override {}
  void AnimationUpdate() override {}
  bool onEvent(GUIEvent &) override { return false; }
};

class TestView final : public View {
public:
  explicit TestView(GUIWindow &window) : View(window, nullptr) {}
  void DrawView() override {}
  void OnPlayerUpdate(PlayerEventType, unsigned int) override {}
  void OnFocus() override {}
  void AnimationUpdate() override {}

protected:
  void ProcessButtonMask(unsigned short, bool) override {}
};

class TestSystem final : public System {
public:
  explicit TestSystem(std::uint32_t random) : random_(random) {}
  unsigned long GetClock() override { return 0; }
  void GetBatteryState(BatteryState &) override {}
  void SetDisplayBrightness(unsigned char) override {}
  void PostQuitMessage() override {}
  unsigned int GetMemoryUsage() override { return 0; }
  void PowerDown() override {}
  void SystemPutChar(int) override {}
  void SystemBootloader() override {}
  void SystemReboot() override {}
  std::uint32_t GetRandomNumber() override { return random_; }
  std::uint32_t Micros() override { return 0; }
  std::uint32_t Millis() override { return 0; }

private:
  std::uint32_t random_;
};

struct RenameFixture {
  TestWindowImp imp;
  TestWindow window{imp};
  TestView view{window};
};

} // namespace

// These deliberately small host definitions let the controller be exercised
// without bringing the legacy renderer and Player singleton into UI2 tests.
GUIWindow::GUIWindow(I_GUIWindowImp &imp) : _imp(&imp) {}
GUIWindow::~GUIWindow() = default;
void GUIWindow::SetColor(GUIColor &) {}
void GUIWindow::ClearTextRect(GUIRect &) {}
void GUIWindow::DrawChar(char, const GUIPoint &, const GUITextProperties &) {}
void GUIWindow::DrawString(const char *, const GUIPoint &,
                           const GUITextProperties &, bool) {}
void GUIWindow::DrawRect(GUIRect &) {}
void GUIWindow::SetCurrentRectColor(GUIColor) {}
GUIRect GUIWindow::GetRect() { return {0, 0, 240, 240}; }
void GUIWindow::Invalidate() {}
void GUIWindow::Flush() {}
void GUIWindow::Lock() {}
void GUIWindow::Unlock() {}
void GUIWindow::Update(bool redraw) { onUpdate(redraw); }
void GUIWindow::ClockTick() { AnimationUpdate(); }
void GUIWindow::PushEvent(GUIEvent &) {}
void GUIWindow::Clear(GUIColor &, bool) {}
bool GUIWindow::DispatchEvent(GUIEvent &event) { return onEvent(event); }
I_GUIGraphics *GUIWindow::GetDC() { return this; }
I_GUIGraphics *GUIWindow::GetGraphics() { return this; }

bool View::initPrivate_ = false;
int View::margin_ = 0;
int View::songRowCount_ = 16;
BatteryState View::batteryState_{};
BatteryState View::latestBatteryState_{};
std::uint32_t View::lastBatteryDisplayFrame_ = 0;
bool View::batteryDisplayInitialized_ = false;

View::View(GUIWindow &window, ViewData *viewData)
    : w_(window), viewData_(viewData), needsRedraw_(false), isVisible_(true),
      vuMeterCount_(0), viewMode_(VM_NORMAL), isDirty_(true),
      viewType_(VT_SONG), hasFocus_(false), prevLeftVU_{}, prevRightVU_{},
      powerButtonPressed_(false), powerButtonHoldCount_(0), mask_(0),
      locked_(false), modalView_(nullptr), modalViewCallback_() {}

void View::SetColor(ColorDefinition) {}
void View::ClearTextRect(int, int, int, int) {}
void View::DrawString(int, int, const char *, const GUITextProperties &) {}
void View::DrawRect(GUIRect &, ColorDefinition) {}
GUIPoint View::GetAnchor() { return {1, 3}; }

TEST_CASE("Rename modal keyboard navigation edits a bounded draft") {
  RenameFixture fixture;
  RenameModalView *modal =
      RenameModalView::Create(fixture.view, "ABCDEFGHIJKLMNOPQRSTUV", 20U);
  REQUIRE(modal != nullptr);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "ABCDEFGHIJKLMNOPQRST");

  modal->ProcessButtonMask(EPBM_DOWN, true);
  CHECK(modal->SnapshotForUi2().focus == ui2::UiDialogFocus::Keyboard);
  CHECK(modal->SnapshotForUi2().selectedKey == 0U);
  modal->ProcessButtonMask(EPBM_RIGHT, true);
  CHECK(modal->SnapshotForUi2().selectedKey == 1U);
  modal->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "ABCDEFGHIJKLMNOPQRST");

  modal->ProcessButtonMask(EPBM_EDIT, true);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "ABCDEFGHIJKLMNOPQRS");
  modal->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "ABCDEFGHIJKLMNOPQRS2");
  modal->Destroy();
}

TEST_CASE("Rename modal case space and delete keys edit the draft") {
  RenameFixture fixture;
  RenameModalView *modal = RenameModalView::Create(fixture.view, "", 16U);
  REQUIRE(modal != nullptr);

  modal->ProcessButtonMask(EPBM_DOWN, true); // digits
  modal->ProcessButtonMask(EPBM_DOWN, true); // Q row
  modal->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "Q");

  modal->ProcessButtonMask(EPBM_DOWN, true); // A row
  modal->ProcessButtonMask(EPBM_DOWN, true); // Z row
  modal->ProcessButtonMask(EPBM_DOWN, true); // special row / ABC
  CHECK(modal->SnapshotForUi2().selectedKey == 36U);
  modal->ProcessButtonMask(EPBM_ENTER, true);
  CHECK_FALSE(modal->SnapshotForUi2().uppercase);
  modal->ProcessButtonMask(EPBM_RIGHT, true); // -
  modal->ProcessButtonMask(EPBM_RIGHT, true); // SPACE
  modal->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "Q ");
  modal->ProcessButtonMask(EPBM_RIGHT, true); // _
  modal->ProcessButtonMask(EPBM_RIGHT, true); // DEL
  modal->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(modal->SnapshotForUi2().ToViewData().value == "Q");
  modal->Destroy();
}

TEST_CASE("Rename modal disables empty Save and Random only changes the draft") {
  RenameFixture fixture;
  TestSystem system(0U);
  System::Install(&system);
  RenameModalView *modal = RenameModalView::Create(fixture.view, "", 16U);
  REQUIRE(modal != nullptr);

  Ui2DialogSnapshot snapshot = modal->SnapshotForUi2();
  CHECK_FALSE(snapshot.saveEnabled);
  CHECK(snapshot.selectedAction == 1U);
  modal->ProcessButtonMask(EPBM_UP, true);
  CHECK(modal->SnapshotForUi2().focus == ui2::UiDialogFocus::Actions);
  modal->ProcessButtonMask(EPBM_LEFT, true);
  CHECK(modal->SnapshotForUi2().selectedAction == 0U);
  modal->ProcessButtonMask(EPBM_RIGHT, true);
  CHECK(modal->SnapshotForUi2().selectedAction == 1U);
  modal->ProcessButtonMask(EPBM_ENTER, true);

  snapshot = modal->SnapshotForUi2();
  CHECK(snapshot.saveEnabled);
  CHECK(snapshot.ToViewData().value == "BAD-SUN");
  CHECK_FALSE(modal->IsFinished());
  modal->Destroy();
}

TEST_CASE("Rename modal Save and Cancel return distinct results") {
  RenameFixture fixture;
  RenameModalView *save = RenameModalView::Create(fixture.view, "ONECYCAC");
  REQUIRE(save != nullptr);
  save->ProcessButtonMask(EPBM_UP, true);
  save->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(save->IsFinished());
  CHECK(save->GetReturnCode() == RenameModalView::SaveReturnCode);
  save->Destroy();

  RenameModalView *cancel = RenameModalView::Create(fixture.view, "ONECYCAC");
  REQUIRE(cancel != nullptr);
  cancel->ProcessButtonMask(EPBM_UP, true);
  cancel->ProcessButtonMask(EPBM_LEFT, true);
  cancel->ProcessButtonMask(EPBM_LEFT, true);
  cancel->ProcessButtonMask(EPBM_ENTER, true);
  CHECK(cancel->IsFinished());
  CHECK(cancel->GetReturnCode() == MBL_CANCEL);
  cancel->Destroy();
}
