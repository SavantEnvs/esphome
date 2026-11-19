// mayhem: TeleInfo (French Linky/historical electricity meter) frame parser, fed over UART.
// First byte selects historical (odd) or standard (even) mode; the rest is what the meter sent.
// Drives the component the way the scheduler does: update() arms a read, loop() consumes bytes,
// splits the frame into groups, checks each CRC and publishes tag/value pairs to listeners.
#include <cstddef>
#include <cstdint>
#include <string>
#include "esphome/components/teleinfo/teleinfo.h"
#include "fuzz_uart.h"

using namespace esphome;

namespace {
class Listener : public teleinfo::TeleInfoListener {
 public:
  explicit Listener(const char *t) { this->tag = t; }
  void publish_val(const std::string &val) override { total += val.size(); }
  size_t total{0};
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  bool historical = data[0] & 1;
  static const char *const TAGS[] = {"ADCO", "PAPP", "IINST", "BASE", "ADSC", "EAST", "SINSTS", "DATE", "PTEC"};
  mayhem_fuzz::FuzzUART uart;
  uart.feed(data + 1, size - 1);
  auto *ti = new teleinfo::TeleInfo(historical);
  ti->set_uart_parent(&uart);
  Listener *listeners[sizeof(TAGS) / sizeof(TAGS[0])];
  for (size_t i = 0; i < sizeof(TAGS) / sizeof(TAGS[0]); i++) {
    listeners[i] = new Listener(TAGS[i]);
    ti->register_teleinfo_listener(listeners[i]);
  }
  ti->setup();
  // Every loop() either consumes input or advances the frame state machine; bound the extra
  // passes needed to process the last frame after the input is drained.
  size_t idle = 0;
  while (idle < 8) {
    size_t before = uart.remaining();
    ti->update();
    ti->loop();
    if (uart.remaining() == before)
      idle++;
    else
      idle = 0;
  }
  for (auto *l : listeners)
    delete l;
  delete ti;
  return 0;
}
