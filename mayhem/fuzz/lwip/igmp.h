#pragma once
// mayhem: the host platform has no lwIP, so ESPHome does not build e131 for host upstream.
// e131_packet.cpp includes lwIP only to join/leave the IGMP multicast group of each universe
// (join_universe_/leave_). This header mocks that piece of the network stack for the fuzz build:
// a minimal ip4_addr_t that converts from network::IPAddress the way lwIP's does on a device,
// and igmp_joingroup/igmp_leavegroup, defined as no-op successes in mayhem/fuzz/fuzz_e131.cpp.
// The packet parser (E131Component::packet_) uses none of it.
#include <arpa/inet.h>
#include <cstdint>
#include "esphome/components/network/ip_address.h"

typedef int8_t err_t;
struct mayhem_lwip_ip4_addr {
  struct in_addr addr {};
  mayhem_lwip_ip4_addr() = default;
  mayhem_lwip_ip4_addr(const esphome::network::IPAddress &ip) {  // NOLINT(google-explicit-constructor)
    char buf[esphome::network::IP_ADDRESS_BUFFER_SIZE];
    inet_aton(ip.str_to(buf), &this->addr);
  }
};
#define ip4_addr_t mayhem_lwip_ip4_addr
#define IP4_ADDR_ANY4 (static_cast<const mayhem_lwip_ip4_addr *>(nullptr))
err_t igmp_joingroup(const mayhem_lwip_ip4_addr *ifaddr, const mayhem_lwip_ip4_addr *groupaddr);
err_t igmp_leavegroup(const mayhem_lwip_ip4_addr *ifaddr, const mayhem_lwip_ip4_addr *groupaddr);
