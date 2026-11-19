// mayhem: Tuya serial protocol — the frames a Tuya MCU (the microcontroller inside Tuya
// switches, dimmers, fans, thermostats, ...) sends to the Wi-Fi module ESPHome replaces.
// TuyaUART::loop() reads the UART byte by byte into handle_char_(), which assembles and checks
// each 55 AA frame (validate_message_) and dispatches it (handle_command_): heartbeat, product
// query, working mode, datapoint reports (handle_datapoints_ -> the registered listeners),
// time and Wi-Fi requests and extended services.
// Input: the bytes the MCU sent. Listeners are registered for datapoints 1-16, as the tuya
// sensor/switch/light/climate platforms do.
// setup() is not called: it only arms the 15 s heartbeat interval in App's scheduler (and drives
// a status pin, not configured here), which a short-lived harness object would leave behind.
// The input itself can still arm scheduler items that capture `this` (handle_command_: the
// Wi-Fi status interval on a CONF_QUERY reply, the datapoint dump timeout on the first
// DATAPOINT_REPORT). The scheduler never runs here, so they are cancelled and recycled before
// the per-input TuyaUART is freed; otherwise each would sit in App.scheduler's to_add_ for the
// rest of the process holding a dangling pointer.
#include <cstddef>
#include <cstdint>
#include <memory>
#include "esphome/components/tuya/tuya_uart.h"
#include "esphome/core/application.h"
#include "fuzz_uart.h"

using namespace esphome;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  mayhem_fuzz::FuzzUART uart;
  uart.feed(data, size);
  auto tuya = std::make_unique<tuya::TuyaUART>();
  tuya->set_uart_parent(&uart);
  size_t total = 0;
  for (uint8_t id = 1; id <= 16; id++) {
    tuya->register_listener(id, [&total](const tuya::TuyaDatapoint &dp) {
      total += dp.len + dp.value_string.size() + dp.value_raw.size();
    });
  }
  // Each loop() drains what is available; a few more passes let the command queue run.
  size_t idle = 0;
  while (idle < 4) {
    size_t before = uart.remaining();
    tuya->loop();
    idle = uart.remaining() == before ? idle + 1 : 0;
  }
  // tuya_uart.cpp: HEARTBEAT_INTERVAL_ID 0, WIFI_STATUS_INTERVAL_ID 1, DATAPOINT_DUMP_TIMEOUT_ID 2,
  // FORCE_TIME_SYNC_INTERVAL_ID 3.
  for (uint32_t id = 0; id <= 3; id++) {
    App.scheduler.cancel_interval(tuya.get(), id);
    App.scheduler.cancel_timeout(tuya.get(), id);
  }
  App.scheduler.process_to_add();  // recycles the cancelled items into the scheduler's pool
  return 0;
}
