// mayhem: packet_transport — ESPHome's device-to-device sensor sharing protocol, carried over
// UDP / UART / ESP-NOW by child transports (udp, uart packet_transport, espnow). A received
// packet is handed to PacketTransport::process_(), which the class documents as the entry
// point child transports call "on receipt of a data packet". This harness is such a child.
// Input: byte 0 = configuration (bit 0: provider "fuzzprov" uses an XXTEA key, bit 1: rolling
// code, bit 2: ping-pong), rest = one received packet.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include "esphome/components/packet_transport/packet_transport.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/host/preferences.h"
#include "esphome/core/application.h"
#include <cstdlib>
#include <memory>
#include <string>

using namespace esphome;

namespace {
// ESPPreferenceObject's backend pointer, through a member pointer named in an explicit
// instantiation (where access is not checked, [temp.spec.general]/6).
template<typename Tag, typename Tag::type M> struct Expose {
  friend typename Tag::type get(Tag) { return M; }
};
struct PrefBackend {
  using type = PreferenceBackend *ESPPreferenceObject::*;
  friend type get(PrefBackend);
};
template struct Expose<PrefBackend, &ESPPreferenceObject::backend_>;

class FuzzTransport : public packet_transport::PacketTransport {
 public:
  // setup() draws the ping key from /dev/urandom; a device announces it in its own packets, so
  // anyone on the network knows it. Fix it so each input's result depends only on the input.
  // setup() allocates its preference backend once per boot and never frees it; free it with the
  // transport, as a reboot would, or every input would leak one.
  ~FuzzTransport() { delete (this->pref_.*get(PrefBackend{})); }
  void pin_ping_key() { this->ping_key_ = 0x6d61796d; }
  void send_packet(const std::vector<uint8_t> &buf) const override {}
  size_t get_max_packet_size() override { return 512; }
  void receive(const uint8_t *data, size_t size) { this->process_(std::span<const uint8_t>(data, size)); }
};
}  // namespace

namespace {
// Once per process, what the host platform's main() and generated setup() do before any
// component's setup(): name the device and install the preferences backend. Preferences go to
// a scratch directory (ESPHOME_PREFDIR), not $HOME.
void init_device() {
  static bool done = false;
  if (done)
    return;
  done = true;
  const char *tmp = getenv("TMPDIR");
  static std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/fuzz_packet_transport.XXXXXX";
  if (mkdtemp(dir.data()) == nullptr)
    abort();
  setenv("ESPHOME_PREFDIR", dir.c_str(), 1);
  App.pre_setup("fuzzdev", 7, "fuzzdev", 7);
  host::setup_preferences();
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  init_device();
  // A fresh boot for every input: the stored rolling code starts unset (as on a new device), and
  // setup() then increments and saves it.
  host::get_preferences()->reset();
  // The remote entities are per input: setup() registers callbacks on them that point into the
  // transport.
  auto s_temp = std::make_unique<sensor::Sensor>();
  auto s_hum = std::make_unique<sensor::Sensor>();
  auto b_door = std::make_unique<binary_sensor::BinarySensor>();
  auto b_status = std::make_unique<binary_sensor::BinarySensor>();
  auto t = std::make_unique<FuzzTransport>();
  uint8_t cfg = data[0];
  t->add_remote_sensor("fuzzprov", "temp", s_temp.get());
  t->add_remote_sensor("fuzzprov", "hum", s_hum.get());
  t->add_remote_binary_sensor("fuzzprov", "door", b_door.get());
#ifdef USE_STATUS_SENSOR
  t->set_provider_status_sensor("fuzzprov", b_status.get());
#endif
  t->add_provider("other");
  if (cfg & 1)
    t->set_provider_encryption("fuzzprov", std::vector<uint8_t>(32, 0x5a));
  t->set_rolling_code_enable(cfg & 2);
  t->set_ping_pong_enable(cfg & 4);
  if (cfg & 4)
    t->set_encryption_key(std::vector<uint8_t>(32, 0xa5));
  t->setup();  // as App does before the first packet: header, stored rolling code, ping key
  if (cfg & 2)
    t->pin_ping_key();  // setup() draws one only with the rolling code on
  // A transport receives into its own (word-aligned) buffer; the decoder decrypts in place as uint32_t.
  std::vector<uint8_t> packet(data + 1, data + size);
  t->receive(packet.data(), packet.size());
  t.reset();
  return 0;
}
