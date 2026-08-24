#ifndef NODEWINDOWIMP_H_
#define NODEWINDOWIMP_H_

#include "Adapters/node/display/display.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"
#include "Foundation/Observable.h"
#include "UI2/Render/UiRgb565Presenter.h"
#include "UIFramework/Interfaces/I_GUIWindowImp.h"
#include "EventQueue.h"
#include <string>

class NodeGUIWindowImp : public I_GUIWindowImp,
                         public I_Observer,
                         public ui2::IUiPresenter {

public:
  NodeGUIWindowImp(GUICreateWindowParams &p);
  virtual ~NodeGUIWindowImp();

public: // I_GUIWindowImp implementation
  virtual void SetColor(GUIColor &) override;
  virtual void DrawRect(GUIRect &) override;
  virtual void DrawChar(const char c, const GUIPoint &pos,
                        const GUITextProperties &props) override;
  virtual void DrawString(const char *string, const GUIPoint &pos,
                          const GUITextProperties &props,
                          bool overlay = false) override;
  virtual void ClearTextRect(GUIRect &r) override;
  virtual GUIRect GetRect() override;
  virtual void Invalidate() override;
  virtual void Flush() override;
  virtual void Lock() override;
  virtual void Unlock() override;
  virtual void Clear(GUIColor &, bool overlay = false) override;
  virtual void ClearRect(GUIRect &);
  virtual void PushEvent(GUIEvent &event) override;

  ui2::PresentResult
  Present(const ui2::UiIndexedSurface &surface,
          const ui2::UiPalette &palette,
          std::span<const ui2::DirtyStrip> strips) override;

  // The switch is intentionally independent of Theme/Device UI state. It is
  // an implementation rollout flag and can be changed without mutating a
  // project or user configuration.
  void SetUi2Enabled(bool enabled);
  bool Ui2Enabled() const { return ui2Enabled_; }

  static void ProcessEvent(NodeEvent &event);
  static void ProcessButtonChange(uint16_t changeMask, uint16_t buttonMask);

protected:
  static color_t GetColor(GUIColor &c);

  virtual void Update(Observable &o, I_ObservableData *d) override;

private:
  static bool WriteUi2Chunk(void *context, std::uint16_t x, std::uint16_t y,
                            std::uint16_t width, std::uint16_t height,
                            const std::uint16_t *pixels);
  bool Ui2ShouldOwnDisplay() const;
  void PresentUi2Frame();
  void RestoreLegacyFrame();

  ui2::UiRgb565Presenter ui2Presenter_;
  ui2::UiApplicationRuntime ui2Runtime_;
  bool ui2Enabled_ = false;
  bool ui2Active_ = false;
  bool remoteUIEnabled_ = 0;
};
#endif
