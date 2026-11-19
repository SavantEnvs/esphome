// mayhem: IR/RF protocol decoders (esphome/components/remote_base). A remote receiver turns the
// pulses it sees on the air into a mark/space timing list (RawTimings) and hands it to every
// configured dumper/decoder. Input: byte 0 = tolerance, byte 1 bit 0 = tolerance mode, then
// little-endian int16 pairs = signed pulse durations in microseconds (mark > 0, space < 0).
// The input is decoded by every protocol dumper upstream declares (generated list, as `dump: all`).
#include <cstddef>
#include <cstdint>
#include <vector>
#include "esphome/components/remote_base/remote_base.h"

using namespace esphome;
using namespace esphome::remote_base;

#define MAYHEM_DUMPER(name) static name dumper_##name;
#include "remote_base_dumpers.inc"
#undef MAYHEM_DUMPER

#define MAYHEM_DUMPER(name) &dumper_##name,
static RemoteReceiverDumperBase *const DUMPERS[] = {
#include "remote_base_dumpers.inc"
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return -1;
  uint32_t tolerance = data[0];
  ToleranceMode mode = (data[1] & 1) ? TOLERANCE_MODE_TIME : TOLERANCE_MODE_PERCENTAGE;
  RawTimings raw;
  for (size_t i = 2; i + 1 < size; i += 2)
    raw.push_back(static_cast<int16_t>(data[i] | (data[i + 1] << 8)));
  for (auto *d : DUMPERS)
    d->dump(RemoteReceiveData(raw, tolerance, mode));
  return 0;
}
