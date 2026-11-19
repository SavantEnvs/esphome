// mayhem: ESPHome's JSON layer (esphome/components/json) — parse_json() on untrusted bodies
// (web server / HTTP request / MQTT payloads), then build_json() re-serializes what was parsed
// through JsonBuilder/SerializationBuffer, so the output size crosses the inline-buffer boundary.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "esphome/components/json/json_util.h"

using namespace esphome;

static void copy_value(JsonVariant dst, JsonVariantConst src, int depth) {
  if (depth > 16) {
    dst.set(nullptr);
    return;
  }
  if (src.is<JsonObjectConst>()) {
    JsonObject o = dst.to<JsonObject>();
    for (JsonPairConst kv : src.as<JsonObjectConst>())
      copy_value(o[kv.key()], kv.value(), depth + 1);
  } else if (src.is<JsonArrayConst>()) {
    JsonArray a = dst.to<JsonArray>();
    for (JsonVariantConst v : src.as<JsonArrayConst>())
      copy_value(a.add<JsonVariant>(), v, depth + 1);
  } else {
    dst.set(src);
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  json::parse_json(data, size, [](JsonObject root) -> bool {
    auto out = json::build_json([&](JsonObject dst) {
      for (JsonPair kv : root)
        copy_value(dst[kv.key()], kv.value(), 0);
    });
    // Touch every serialized byte so a short/overlong buffer is caught here.
    volatile size_t sum = 0;
    const char *p = out.c_str();
    for (size_t i = 0; i < out.size(); i++)
      sum += static_cast<unsigned char>(p[i]);
    return std::strlen(p) == out.size();
  });
  return 0;
}
