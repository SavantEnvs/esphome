#pragma once
// mayhem: feature flags for the host-platform fuzz/test build. Upstream's own build generates
// this file from the YAML config (esphome/core/config.py + each component's to_code); the
// checked-in esphome/core/defines.h is an IDE-only superset and is NOT what a real build uses.
// mayhem/build.sh stages a symlink tree of esphome/ and drops this file in as
// esphome/core/defines.h, exactly as the esphome build tool does.
#include "esphome/core/macros.h"
#define ESPHOME_BOARD "host"
#define ESPHOME_VARIANT "HOST"
#define ESPHOME_COMPONENT_COUNT 32
#define ESPHOME_THREAD_MULTI_ATOMICS
#define USE_NATIVE_64BIT_TIME
#define ESPHOME_ENTITY_SENSOR_COUNT 16
#define ESPHOME_ENTITY_TEXT_SENSOR_COUNT 16
#define ESPHOME_ENTITY_BINARY_SENSOR_COUNT 16
#define USE_SENSOR
#define USE_TEXT_SENSOR
#define USE_BINARY_SENSOR
// light: the addressable strip e131 paints, and the API's LightCommandRequest
#define USE_LIGHT
#define ESPHOME_ENTITY_LIGHT_COUNT 1
#define USE_JSON
#define USE_ESPHOME_HOST_MAC_ADDRESS {152, 53, 105, 171, 246, 121}
#define USE_STATUS_SENSOR

#ifdef MAYHEM_TEST_BUILD
// Upstream's C++ unit-test build (script/cpp_unit_test.py) adds the logger component and
// builds with -DESPHOME_DEBUG; tests/components/main.cpp sets the logger up before RUN_ALL_TESTS.
#define USE_LOGGER
#define ESPHOME_LOGGER_TX_BUFFER_SIZE 512
#define ESPHOME_TASK_LOG_BUFFER_SIZE 64
#define USE_ESPHOME_TASK_LOG_BUFFER
#endif
#define USE_JSON_ARENA
#define MODBUS_ON_REQUEST_COUNT 2
#define MODBUS_ON_RESPONSE_COUNT 2

// Native API (plaintext transport) over the host platform's BSD sockets.
#define USE_NETWORK
#define USE_NETWORK_IPV4 true
#define USE_NETWORK_IPV6 false
#define USE_SOCKET_IMPL_BSD_SOCKETS
#define USE_API
#define USE_API_PLAINTEXT
#define USE_API_HOMEASSISTANT_STATES
#define USE_API_USER_DEFINED_ACTIONS
#define MAX_API_CONNECTIONS 4
#define API_MAX_SEND_QUEUE 8
