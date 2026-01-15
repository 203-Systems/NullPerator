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
  virtual void SetColor(GUIColor &);
  virtual void DrawRect(GUIRect &);
  virtual void DrawChar(const char c, GUIPoint &pos, GUITextProperties &);
  virtual void DrawString(const char *string, GUIPoint &pos,
                          GUITextProperties &, bool overlay = false);
  virtual void ClearTextRect(GUIRect &r) override;
  virtual GUIRect GetRect();
  virtual void Invalidate();
  virtual void Flush();
  virtual void Lock();
  virtual void Unlock();
  virtual void Clear(GUIColor &, bool overlay = false);
  virtual void ClearRect(GUIRect &);
  virtual void PushEvent(GUIEvent &event);

  static void ProcessEvent(NodeEvent &event);
  static void ProcessButtonChange(uint16_t changeMask, uint16_t buttonMask);

protected:
  static color_t GetColor(GUIColor &c);

  virtual void Update(Observable &o, I_ObservableData *d);

private:
  bool remoteUIEnabled_ = 0;
};
#endif
