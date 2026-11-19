// mayhem: E1.31 (sACN) receiver — ESPHome's e131 component, which listens for unauthenticated
// UDP (multicast or unicast, port 5568) DMX frames from anyone on the LAN and paints them onto
// addressable LED strips through an E131AddressableLightEffect.
//
// E131Component::loop() reads each queued datagram into a 1460-byte buffer and, per datagram,
// calls packet_(buf, len, universe, packet) and, if it parsed, process_(universe, packet), which
// hands the DMX values to every registered light effect. This harness does exactly that for each
// datagram in the input (the UDP socket itself is the only thing replaced), with one
// E131AddressableLightEffect started on an in-memory 300-pixel strip, as a YAML config with
// `e131:` and an `addressable_light` effect `e131:` would set up.
//
// Input: byte 0 = configuration (bits 0-1: channels MONO/RGB/RGBW, bits 2-4: first universe
// 1..8), then datagrams, each as a 2-byte big-endian length followed by that many bytes; the last
// datagram takes whatever is left. Like recv() into loop()'s 1460-byte buffer, a longer datagram
// is truncated to 1460 bytes. Each datagram is copied into a heap buffer of exactly its length,
// so a read past what was received is reported (on a device it reads stale bytes of the rest of
// loop()'s stack buffer).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include "esphome/components/e131/e131.h"
#include "esphome/components/e131/e131_addressable_light_effect.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_state.h"
#include "lwip/igmp.h"

using namespace esphome;

// The host has no lwIP (see mayhem/fuzz/lwip/igmp.h): joining/leaving a multicast group succeeds.
err_t igmp_joingroup(const mayhem_lwip_ip4_addr *, const mayhem_lwip_ip4_addr *) { return 0; }
err_t igmp_leavegroup(const mayhem_lwip_ip4_addr *, const mayhem_lwip_ip4_addr *) { return 0; }

namespace {

// packet_() and process_() are what loop() calls on each received datagram; E131Component is
// `final`, so they are reached through member pointers named in an explicit instantiation
// (where access is not checked, [temp.spec.general]/6) instead of a subclass.
template<typename Tag, typename Tag::type M> struct Expose {
  friend typename Tag::type get(Tag) { return M; }
};
struct PacketFn {
  using type = bool (e131::E131Component::*)(const uint8_t *, size_t, int &, e131::E131Packet &);
  friend type get(PacketFn);
};
struct ProcessFn {
  using type = bool (e131::E131Component::*)(int, const e131::E131Packet &);
  friend type get(ProcessFn);
};
template struct Expose<PacketFn, &e131::E131Component::packet_>;
template struct Expose<ProcessFn, &e131::E131Component::process_>;

constexpr int32_t STRIP_LEDS = 300;
constexpr size_t RECV_BUF = 1460;  // E131Component::loop()'s receive buffer

// An RGBW strip in memory, like the upstream light tests' strips (tests/components/light/).
class Strip : public light::AddressableLight {
 public:
  Strip()
      : accessor_impl_(this->buffer_.data(), this->buffer_.data() + 4 * STRIP_LEDS, light::ChannelColors{0, 1, 2, 3},
                       &this->correction_) {
    this->accessor_ = &this->accessor_impl_;
  }
  int32_t size() const override { return STRIP_LEDS; }
  void clear_effect_data() override {}
  light::LightTraits get_traits() override { return {}; }
  void write_state(light::LightState *state) override {}

 protected:
  std::array<uint8_t, 5 * STRIP_LEDS> buffer_{};
  light::CorrectedBufferColorAccessor accessor_impl_;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  static const e131::E131LightChannels CHANNELS[] = {e131::E131_MONO, e131::E131_RGB, e131::E131_RGBW,
                                                     e131::E131_RGBW};
  const uint8_t cfg = data[0];

  auto strip = std::make_unique<Strip>();
  auto state = std::make_unique<light::LightState>(strip.get());
  strip->setup_state(state.get());
  auto e131 = std::make_unique<e131::E131Component>();
  e131->set_method(e131::E131_UNICAST);
  auto effect = std::make_unique<e131::E131AddressableLightEffect>("fuzz");
  effect->set_e131(e131.get());
  effect->set_channels(CHANNELS[cfg & 3]);
  effect->set_first_universe(1 + ((cfg >> 2) & 7));
  effect->init_internal(state.get());
  effect->start_internal();  // registers the effect (and its universes) with the component

  const uint8_t *p = data + 1;
  size_t left = size - 1;
  while (left > 0) {
    size_t len = left;
    if (left >= 2) {
      len = std::min<size_t>((size_t(p[0]) << 8) | p[1], left - 2);
      p += 2;
      left -= 2;
    }
    const size_t received = std::min(len, RECV_BUF);
    std::unique_ptr<uint8_t[]> buf(new uint8_t[received ? received : 1]);
    memcpy(buf.get(), p, received);
    p += len;
    left -= len;

    e131::E131Packet packet;
    int universe = 0;
    if (((*e131).*get(PacketFn{}))(buf.get(), received, universe, packet))
      ((*e131).*get(ProcessFn{}))(universe, packet);
  }

  effect->stop();
  return 0;
}
