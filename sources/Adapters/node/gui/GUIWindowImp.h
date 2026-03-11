#ifndef NODEWINDOWIMP_H_
#define NODEWINDOWIMP_H_

#include "Adapters/node/display/display.h"
#include "Foundation/Observable.h"
#include "UIFramework/Interfaces/I_GUIWindowImp.h"
#include "EventQueue.h"
#include <string>

class NodeGUIWindowImp : public I_GUIWindowImp, public I_Observer {

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

  static void ProcessEvent(NodeEvent &event);
  static void ProcessButtonChange(uint16_t changeMask, uint16_t buttonMask);

protected:
  static color_t GetColor(GUIColor &c);

  virtual void Update(Observable &o, I_ObservableData *d) override;

private:
  bool remoteUIEnabled_ = 0;
};
#endif
