#include "MidiInDevice.h"

#include "System/Console/Trace.h"

NodeMidiInDevice::NodeMidiInDevice(const char *name) : MidiInDevice(name) {
  Trace::Log("MIDI", "Created stub MIDI input device %s", name);
}

NodeMidiInDevice::~NodeMidiInDevice() { closeDriver(); }

bool NodeMidiInDevice::initDriver() { return true; }

void NodeMidiInDevice::closeDriver() {}

bool NodeMidiInDevice::Start() { return startDriver(); }

void NodeMidiInDevice::Stop() { stopDriver(); }

bool NodeMidiInDevice::startDriver() { return true; }

void NodeMidiInDevice::stopDriver() {}

void NodeMidiInDevice::poll() {}
