#include "GUIWindowImp.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include <string.h>
#include "Adapters/node/utils/utils.h"
#include "Application/Utils/char.h"
#include "UIFramework/BasicDatas/GUIEvent.h"
#include "EventManager.h"
#ifdef USB_REMOTE_UI
#include "RemoteUI.h"
#endif
#include "freertos/semphr.h"
#include <string>
#include "esp_log.h"

#define to_rgb565(color)                                                       \
  ((color._r & 0b11111000) << 8) | ((color._g & 0b11111100) << 3) |            \
      (color._b >> 3)

static GUIEventPadButtonType eventMappingPico[10] = {
    EPBT_LEFT,  
    EPBT_DOWN,  
    EPBT_RIGHT, 
    EPBT_UP,    
    EPBT_L,     
    EPBT_B,     
    EPBT_A,     
    EPBT_R,     
    EPBT_START, 
    EPBT_SELECT
};

static GUIEventPadButtonType *eventMapping = eventMappingPico;

NodeGUIWindowImp *instance_;
static SemaphoreHandle_t s_displayMutex = nullptr;

NodeGUIWindowImp::NodeGUIWindowImp(GUICreateWindowParams &p) {
  display_init();
  instance_ = this;
  if (s_displayMutex == nullptr) {
    s_displayMutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(s_displayMutex != nullptr);
  }

  Config *config = Config::GetInstance();

  auto remoteUIVar =
      (WatchedVariable *)config->FindVariable(FourCC::VarRemoteUI);

  // register to receive updates to remoteui setting
  remoteUIVar->AddObserver(*this);
  auto remoteui = remoteUIVar->GetInt();
  remoteUIEnabled_ = remoteui != 0;

  auto uiFontVar = (WatchedVariable *)config->FindVariable(FourCC::VarUIFont);
  // register to receive updates to remoteui setting
  uiFontVar->AddObserver(*this);
  display_set_font_index(uiFontVar->GetInt());
};

NodeGUIWindowImp::~NodeGUIWindowImp() {}

void NodeGUIWindowImp::DrawChar(const char c, const GUIPoint &pos,
                                const GUITextProperties &p) {
  //  Trace::Debug("Draw char \"%c\" at pos x:%ld (%ld), y:%ld (%ld) - invert: %d", c, pos._x, pos._x / 8, pos._y, pos._y / 8, p.invert_);

  uint8_t x = pos._x / 8;
  uint8_t y = pos._y / 8;
  display_set_cursor(x, y);
  display_putc(c, p.invert_);
#ifdef USB_REMOTE_UI
  if (remoteUIEnabled_) {
    char remoteUIBuffer[6];
    remoteUIBuffer[0] = REMOTE_UI_CMD_MARKER;
    remoteUIBuffer[1] = DRAW_CMD;
    remoteUIBuffer[2] = c;
    remoteUIBuffer[3] = x + 32;
    remoteUIBuffer[4] = y + 32;
    remoteUIBuffer[5] = p.invert_ ? 127 : 32;
    sendToUSBCDC(remoteUIBuffer, 6);
  }
#endif
}

void NodeGUIWindowImp::DrawString(const char *string, const GUIPoint &pos,
                                  const GUITextProperties &p, bool overlay) {
  Trace::Debug("draw string");
  display_set_cursor(pos._x, pos._y);
  display_print(string, p.invert_);
};

void NodeGUIWindowImp::DrawRect(GUIRect &r) {
  Trace::Debug("GUI DrawRect call");
};

void NodeGUIWindowImp::ClearTextRect(GUIRect &r) { ClearRect(r); }

void NodeGUIWindowImp::Clear(GUIColor &c, bool overlay) {
  Lock();
  color_t backgroundColor = GetColor(c);
  display_set_background(backgroundColor);
  display_clear(backgroundColor);
#ifdef USB_REMOTE_UI
  if (remoteUIEnabled_) {
    char remoteUIBuffer[3];
    remoteUIBuffer[0] = REMOTE_UI_CMD_MARKER;
    remoteUIBuffer[1] = CLEAR_CMD;
    remoteUIBuffer[2] = backgroundColor + UART_ASCII_OFFSET;
    sendToUSBCDC(remoteUIBuffer, 3);
  }
#endif
  Unlock();
};

void NodeGUIWindowImp::ClearRect(GUIRect &r) {
  Trace::Debug("GUI ClearRect call");
};

color_t NodeGUIWindowImp::GetColor(GUIColor &c) {
  // Palette index should always be < 16. Wont check it.
  // TODO: should not be redefining the palette colors every call
  display_set_palette_color(c._paletteIndex, to_rgb565(c));
  return (color_t)c._paletteIndex;
}

void NodeGUIWindowImp::SetColor(GUIColor &c) {
  display_set_foreground(GetColor(c));
#ifdef USB_REMOTE_UI
  if (remoteUIEnabled_) {
    char remoteUIBuffer[3];
    remoteUIBuffer[0] = REMOTE_UI_CMD_MARKER;
    remoteUIBuffer[1] = SETCOLOR_CMD;
    remoteUIBuffer[2] = GetColor(c) + UART_ASCII_OFFSET;
    sendToUSBCDC(remoteUIBuffer, 3);
  }
#endif
};

void NodeGUIWindowImp::Lock() {
  if (s_displayMutex != nullptr) {
    xSemaphoreTakeRecursive(s_displayMutex, portMAX_DELAY);
  }
};

void NodeGUIWindowImp::Unlock() {
  if (s_displayMutex != nullptr) {
    xSemaphoreGiveRecursive(s_displayMutex);
  }
};

void NodeGUIWindowImp::Flush() {
  Lock();
  display_draw_changed();
  Unlock();
};

void NodeGUIWindowImp::Invalidate() {
  NodeEventManager::PostEvent(PICO_FLUSH);
};

void NodeGUIWindowImp::PushEvent(GUIEvent &event) {
  Trace::Debug("GUI PushEvent");
};

GUIRect NodeGUIWindowImp::GetRect() {
  Trace::Debug("GUI GetRect");
  return GUIRect(0, 0, 320, 240);
}

void NodeGUIWindowImp::ProcessEvent(NodeEvent &event) {
  switch (event.type_) {
  case PICO_REDRAW:
    instance_->_window->ClockTick();
    break;
  case PICO_FLUSH:
    instance_->_window->Flush();
    break;
  case PICO_CLOCK:
    instance_->_window->ClockTick();
    break;
  case LAST:
    break;
  }
}

void NodeGUIWindowImp::ProcessButtonChange(uint16_t changeMask,
                                                  uint16_t buttonMask) {
  int e = 1;
  System *system = System::GetInstance();
  unsigned long now = system->GetClock();
  for (int i = 0; i < 10; i++) {
    if (changeMask & e) {
      GUIEventType type = (buttonMask & e) ? ET_PADBUTTONDOWN : ET_PADBUTTONUP;

      GUIEvent event(eventMapping[i], type, now, 0, 0, 0);
      instance_->_window->DispatchEvent(event);
    }
    e = e << 1;
  }
}

void NodeGUIWindowImp::Update(Observable &o, I_ObservableData *d) {
  WatchedVariable &v = (WatchedVariable &)o;
  switch (v.GetID()) {
  case FourCC::VarRemoteUI: {
    auto remoteui = v.GetInt();
    remoteUIEnabled_ = remoteui != 0;
  } break;
  case FourCC::VarUIFont: {
    auto uifont = v.GetInt();
    display_set_font_index(uifont);
  } break;
  }
}
