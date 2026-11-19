// mayhem: Grow/R30x fingerprint reader driver — parses the reader's reply packets from UART.
// Input: byte 0 = number of actions, bytes 1..8 = action list, rest = the reader's replies, each
// prefixed with a 2-byte big-endian length (one reply is released per command sent).
// setup() runs the password check + system-parameter read; then each action calls one public
// entry point (update/scan, enroll, delete, LED control), each of which sends a command and
// parses the reply through transfer_().
#include <cstddef>
#include <cstdint>
#include "esphome/components/fingerprint_grow/fingerprint_grow.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "fuzz_uart.h"

using namespace esphome;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 9)
    return -1;
  // The sensor entities only receive published values; their state is never read back here.
  static sensor::Sensor count, status, capacity, security, last_id, last_conf;
  static binary_sensor::BinarySensor enrolling;
  mayhem_fuzz::FuzzUART uart;
  uart.feed_replies(data + 9, size - 9);
  auto *fp = new fingerprint_grow::FingerprintGrowComponent();
  fp->set_uart_parent(&uart);
  fp->set_fingerprint_count_sensor(&count);
  fp->set_status_sensor(&status);
  fp->set_capacity_sensor(&capacity);
  fp->set_security_level_sensor(&security);
  fp->set_last_finger_id_sensor(&last_id);
  fp->set_last_confidence_sensor(&last_conf);
  fp->set_enrolling_binary_sensor(&enrolling);
  fp->setup();
  uint8_t nops = data[0] % 9;
  for (uint8_t i = 0; i < nops && !uart.replies_drained(); i++) {
    uint8_t op = data[1 + i];
    switch (op % 8) {
      case 0:
      case 1:
        fp->update();
        break;
      case 2:
        fp->enroll_fingerprint(op >> 3, 1 + (op >> 6));
        fp->update();
        break;
      case 3:
        fp->delete_fingerprint(op);
        break;
      case 4:
        fp->delete_all_fingerprints();
        break;
      case 5:
        fp->led_control(op & 0x80);
        break;
      case 6:
        fp->aura_led_control(op >> 3, 1, 2, 3);
        break;
      case 7:
        fp->finish_enrollment(op >> 3);
        break;
    }
  }
  delete fp;
  return 0;
}
