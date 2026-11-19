// mayhem: HLK-LD2450 24 GHz radar driver — parses the radar's UART stream (periodic target
// frames and command ACKs: firmware version, MAC, zone config) in LD2450Component::loop(),
// publishing targets to the sensor entities. Input: everything the radar sent.
#include <cstddef>
#include <cstdint>
#include "esphome/components/ld2450/ld2450.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "fuzz_uart.h"

using namespace esphome;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // Entities only receive published values; nothing reads their state back.
  static binary_sensor::BinarySensor connected, moving, still, target;
  static sensor::Sensor moving_count, still_count, target_count;
  static text_sensor::TextSensor mac, version;
  mayhem_fuzz::FuzzUART uart;
  uart.feed(data, size);
  auto *radar = new ld2450::LD2450Component();
  radar->set_uart_parent(&uart);
  radar->set_connected_binary_sensor(&connected);
  radar->set_moving_target_binary_sensor(&moving);
  radar->set_still_target_binary_sensor(&still);
  radar->set_target_binary_sensor(&target);
  radar->set_moving_target_count_sensor(&moving_count);
  radar->set_still_target_count_sensor(&still_count);
  radar->set_target_count_sensor(&target_count);
  radar->set_mac_text_sensor(&mac);
  radar->set_version_text_sensor(&version);
  radar->setup();
  size_t idle = 0;
  while (idle < 4) {
    size_t before = uart.remaining();
    radar->loop();
    idle = uart.remaining() == before ? idle + 1 : 0;
  }
  delete radar;
  return 0;
}
