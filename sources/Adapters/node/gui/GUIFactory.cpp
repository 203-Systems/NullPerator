#include "GUIFactory.h"
#include "EventManager.h"
#include "GUIWindowImp.h"

GUIFactory::GUIFactory(){};

I_GUIWindowImp &GUIFactory::CreateWindowImp(GUICreateWindowParams &p) {
  alignas(NodeGUIWindowImp) static char guiImpMemBuf[sizeof(NodeGUIWindowImp)];
  return *(new (guiImpMemBuf) NodeGUIWindowImp(p));
}

EventManager *GUIFactory::GetEventManager() {
  return NodeEventManager::GetInstance();
}
