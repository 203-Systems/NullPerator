
#include "picoTrackerMidiDevice.h"
#include "Adapters/esp32/platform/platform.h"
#include "Adapters/esp32/platform/gpio.h"
#include "System/Console/Trace.h"
#include "driver/uart.h"

picoTrackerMidiOutDevice::picoTrackerMidiOutDevice(const char *name)
    : MidiOutDevice(name) {}

#define MIDI_UART UART_NUM_2
bool picoTrackerMidiOutDevice::Init() { 
   uart_config_t uart_config = {
      .baud_rate = 31250,
      .data_bits = UART_DATA_8_BITS,
      .parity    = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
  };

  int rx_buffer_size = 2048; // TODO: Optimize this value - No MIDI in feature yet

  ESP_ERROR_CHECK(uart_driver_install(MIDI_UART, rx_buffer_size, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(MIDI_UART, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(MIDI_UART, MIDI_OUT_PIN, MIDI_IN_PIN, -1, -1));
  return true; 
}

void picoTrackerMidiOutDevice::Close(){};

bool picoTrackerMidiOutDevice::Start() { return true; };

void picoTrackerMidiOutDevice::Stop() {}

void picoTrackerMidiOutDevice::SendMessage(MidiMessage &msg) {
  uart_write_bytes(MIDI_UART, &msg.status_, 1);

  if (msg.status_ < 0xF0) {
    uart_write_bytes(MIDI_UART, &msg.data1_, 1);
    if (msg.data2_ != MidiMessage::UNUSED_BYTE) {
      uart_write_bytes(MIDI_UART, &msg.data2_, 1);
    }
  }
}
