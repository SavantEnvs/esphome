// mayhem: Modbus RTU over UART (esphome/components/modbus). Byte 0 picks the role:
// odd = ModbusServerHub (ESPHome as a slave: parses requests from the bus master and answers them
// through a registered ModbusServerDevice), even = ModbusClientHub (ESPHome as master: parses
// whatever frames arrive on the bus). The rest is the bus traffic, handed to the hub's loop().
#include <cstddef>
#include <cstdint>
#include "esphome/components/modbus/modbus.h"
#include "fuzz_uart.h"

using namespace esphome;
using namespace esphome::modbus;

namespace {
// A server device with registers and coils at every address, so requests reach the response builders.
class Device : public ModbusServerDevice {
 public:
  explicit Device(uint8_t address) { this->set_address(address); }
  ResponseStatus on_read_registers(uint16_t start, uint16_t count, RegisterValues &regs) override {
    if (start & 0x8000)
      return ExceptionCode::ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < count && regs.size() < regs.capacity(); i++)
      regs.push_back(start + i);
    return std::nullopt;
  }
  ResponseStatus on_write_registers(uint16_t start, const RegisterValues &regs) override {
    for (auto v : regs)
      sum += v;
    return std::nullopt;
  }
  ResponseStatus on_read_bits(uint16_t start, MutablePackedBits bits) override {
    for (uint16_t i = 0; i < bits.size(); i++)
      bits.set(i, (start + i) & 1);
    return std::nullopt;
  }
  ResponseStatus on_write_coils(uint16_t start, PackedBits bits) override {
    for (uint16_t i = 0; i < bits.size(); i++)
      sum += bits[i];
    return std::nullopt;
  }
  uint32_t sum{0};
};

class ServerHub : public ModbusServerHub {
 public:
  bool tx_blocked() override { return false; }
};
class ClientHub : public ModbusClientHub {
 public:
  bool tx_blocked() override { return false; }
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return -1;
  mayhem_fuzz::FuzzUART uart;
  uart.feed(data + 2, size - 2);
  Device device(data[1]);
  ServerHub *server = nullptr;
  ClientHub *client = nullptr;
  Modbus *hub;
  if (data[0] & 1) {
    hub = server = new ServerHub();
    server->register_device(&device);
  } else {
    hub = client = new ClientHub();
  }
  hub->set_uart_parent(&uart);
  hub->setup();
  size_t idle = 0;
  while (idle < 4) {
    size_t before = uart.remaining();
    hub->loop();
    idle = uart.remaining() == before ? idle + 1 : 0;
  }
  delete server;
  delete client;
  return 0;
}
