#include "GUIWindowImp.h"
#include "Application/AppWindow.h"
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
#include "esp_attr.h"
#include <string>
#include "esp_log.h"

#ifndef PICOTRACKER_UI2_DEFAULT
#define PICOTRACKER_UI2_DEFAULT 0
#endif

#define to_rgb565(color)                                                       \
  ((color._r & 0b11111000) << 8) | ((color._g & 0b11111100) << 3) |            \
      (color._b >> 3)

static GUIEventPadButtonType eventMappingPico[11] = {
    EPBT_LEFT,  
    EPBT_DOWN,  
    EPBT_RIGHT, 
    EPBT_UP,    
    EPBT_L,     
    EPBT_B,     
    EPBT_A,     
    EPBT_R,     
    EPBT_START, 
    EPBT_SELECT,
    EPBT_POWER
};

static GUIEventPadButtonType *eventMapping = eventMappingPico;

DMA_ATTR static std::uint16_t
    s_ui2TransferPixels[ui2::UiRgb565Presenter::kTransferPixels];

NodeGUIWindowImp *instance_;
static SemaphoreHandle_t s_displayMutex = nullptr;

NodeGUIWindowImp::NodeGUIWindowImp(GUICreateWindowParams &p)
    : ui2Presenter_(s_ui2TransferPixels,
                    ui2::UiRgb565Presenter::kTransferPixels,
                    &NodeGUIWindowImp::WriteUi2Chunk, this,
                    ui2::UiRgb565ByteOrder::MostSignificantByteFirst),
      ui2Runtime_(*this),
      ui2Enabled_(PICOTRACKER_UI2_DEFAULT != 0) {
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
  // AppWindow continues building the complete legacy character surface while
  // UI2 owns the LCD. Suppressing only the SPI flush gives us an immediate,
  // reversible fallback without paying for two physical redraws per tick.
  if (Ui2ShouldOwnDisplay()) {
    return;
  }
  Lock();
  if (ui2Active_) {
    display_draw_screen();
    ui2Active_ = false;
    ui2Runtime_.Invalidate();
  } else {
    display_draw_changed();
  }
  Unlock();
};

void NodeGUIWindowImp::Invalidate() {
  NodeEventManager::PostEvent(FLUSH);
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
  case REDRAW:
    instance_->_window->Update(true);
    break;
  case FLUSH:
    instance_->_window->Update(false);
    break;
  case CLOCK:
    instance_->_window->ClockTick();
    break;
  case LAST:
    break;
  }
  instance_->PresentUi2Frame();
}

void NodeGUIWindowImp::ProcessButtonChange(uint16_t changeMask,
                                                  uint16_t buttonMask) {
  int e = 1;
  System *system = System::GetInstance();
  unsigned long now = system->GetClock();
  for (int i = 0; i < 11; i++) {
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

ui2::PresentResult
NodeGUIWindowImp::Present(const ui2::UiIndexedSurface &surface,
                          const ui2::UiPalette &palette,
                          std::span<const ui2::DirtyStrip> strips) {
  Lock();
  const ui2::PresentResult result =
      ui2Presenter_.Present(surface, palette, strips);
  Unlock();
  return result;
}

bool NodeGUIWindowImp::WriteUi2Chunk(void *, std::uint16_t x,
                                     std::uint16_t y, std::uint16_t width,
                                     std::uint16_t height,
                                     const std::uint16_t *pixels) {
  return display_draw_rgb565_region(x, y, width, height, pixels);
}

bool NodeGUIWindowImp::Ui2ShouldOwnDisplay() const {
  if (!ui2Enabled_ || _window == nullptr) return false;
  return ui2Runtime_.Supports(*static_cast<AppWindow *>(_window));
}

void NodeGUIWindowImp::RestoreLegacyFrame() {
  Lock();
  display_draw_screen();
  Unlock();
  ui2Active_ = false;
  ui2Runtime_.Invalidate();
}

void NodeGUIWindowImp::PresentUi2Frame() {
  if (!Ui2ShouldOwnDisplay()) {
    // Do not restore here: REDRAW is posted immediately after input and the
    // legacy character surface may still describe the previous view. The next
    // AppWindow::Flush restores the fully redrawn legacy frame atomically.
    return;
  }

  if (!ui2Active_) ui2Runtime_.Invalidate();
  const ui2::PresentResult result =
      ui2Runtime_.Present(*static_cast<AppWindow *>(_window));
  if (result == ui2::PresentResult::Presented) {
    ui2Active_ = true;
  } else if (result == ui2::PresentResult::Failed) {
    ESP_LOGE("NODE_UI2", "UI2 presenter failed; restoring legacy display");
    ui2Enabled_ = false;
    RestoreLegacyFrame();
  }
}

void NodeGUIWindowImp::SetUi2Enabled(bool enabled) {
  if (ui2Enabled_ == enabled) return;
  ui2Enabled_ = enabled;
  ui2Runtime_.Invalidate();
  if (!enabled && ui2Active_) RestoreLegacyFrame();
}
