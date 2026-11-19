// mayhem: SML (Smart Message Language, German smart meters) over UART.
// Byte 0 odd: the rest is the meter's serial stream through Sml::loop() — escape-sequence framing,
// CRC check, then the SML file parser and OBIS extraction published to a listener.
// Byte 0 even: the rest is one SML file handed straight to the parser class Sml::loop() uses
// (SmlFile → get_obis_info()), so the TLV parser is reached without having to forge a CRC.
#include <cstddef>
#include <cstdint>
#include <vector>
#include "esphome/components/sml/sml.h"
#include "esphome/components/sml/sml_parser.h"
#include "fuzz_uart.h"

using namespace esphome;

namespace {
class Listener : public sml::SmlListener {
 public:
  Listener() : sml::SmlListener("", "") {}
  void publish_val(const sml::ObisInfo &info) override { total += info.value.size() + info.code_repr().size(); }
  size_t total{0};
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  if (data[0] & 1) {
    mayhem_fuzz::FuzzUART uart;
    uart.feed(data + 1, size - 1);
    auto *s = new sml::Sml();
    s->set_uart_parent(&uart);
    Listener listener;
    s->register_sml_listener(&listener);
    while (!uart.drained())
      s->loop();
    delete s;
  } else {
    std::vector<uint8_t> buf(data + 1, data + size);
    sml::SmlFile file{sml::BytesView(buf)};
    volatile size_t n = 0;
    for (auto &info : file.get_obis_info())
      n += info.value.size() + info.code_repr().size() + info.server_id.size();
  }
  return 0;
}
