// mayhem: ESPHome native API, plaintext transport (TCP port 6053, unauthenticated) — the frame
// reader APIPlaintextFrameHelper::read_packet() over a real loopback TCP connection, then each
// received frame is decoded into the generated protobuf message class for its type, as the
// connection's dispatch does (api_pb2_service.cpp read_message → <Msg>::decode()).
// Input: the bytes a client sends on the connection.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "esphome/components/api/api_frame_helper_plaintext.h"
#include "esphome/components/api/api_pb2.h"
#include "esphome/components/socket/socket.h"

using namespace esphome;
using namespace esphome::api;

static int g_listen_fd = -1;
static struct sockaddr_in g_addr {};

// A socket call that fails here is the host running out of something (ports, descriptors), not
// the input: stop rather than keep running without testing anything.
static void check(bool ok, const char *what) {
  if (!ok) {
    perror(what);
    abort();
  }
}

static void listen_once() {
  g_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  check(g_listen_fd >= 0, "socket");
  int opt = 1;
  ::setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  g_addr.sin_family = AF_INET;
  g_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  g_addr.sin_port = 0;
  check(::bind(g_listen_fd, reinterpret_cast<struct sockaddr *>(&g_addr), sizeof(g_addr)) == 0, "bind");
  check(::listen(g_listen_fd, 4) == 0, "listen");
  socklen_t len = sizeof(g_addr);
  check(::getsockname(g_listen_fd, reinterpret_cast<struct sockaddr *>(&g_addr), &len) == 0, "getsockname");
}

template<typename T> static void decode_as(const uint8_t *data, size_t len) {
  T msg;
  msg.decode(data, len);
}

static void dispatch(uint16_t type, const uint8_t *data, size_t len) {
  switch (type) {
    case HelloRequest::MESSAGE_TYPE:
      decode_as<HelloRequest>(data, len);
      break;
    case DisconnectRequest::MESSAGE_TYPE:
      decode_as<DisconnectRequest>(data, len);
      break;
    case SubscribeLogsRequest::MESSAGE_TYPE:
      decode_as<SubscribeLogsRequest>(data, len);
      break;
    case HomeAssistantStateResponse::MESSAGE_TYPE:
      decode_as<HomeAssistantStateResponse>(data, len);
      break;
    case GetTimeResponse::MESSAGE_TYPE:
      decode_as<GetTimeResponse>(data, len);
      break;
    case ExecuteServiceRequest::MESSAGE_TYPE:
      decode_as<ExecuteServiceRequest>(data, len);
      break;
#ifdef USE_LIGHT
    case LightCommandRequest::MESSAGE_TYPE:
      decode_as<LightCommandRequest>(data, len);
      break;
#endif
    default:
      break;
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (g_listen_fd < 0)
    listen_once();
  int client = ::socket(AF_INET, SOCK_STREAM, 0);
  check(client >= 0, "socket");
  check(::connect(client, reinterpret_cast<struct sockaddr *>(&g_addr), sizeof(g_addr)) == 0, "connect");
  int server = ::accept(g_listen_fd, nullptr, nullptr);
  check(server >= 0, "accept");
  // Reset instead of a graceful close, so finished connections do not pile up in TIME_WAIT.
  struct linger lg {1, 0};
  ::setsockopt(server, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
  // Loopback buffers hold far more than a fuzz input, so this write never blocks.
  size_t off = 0;
  while (off < size) {
    ssize_t n = ::write(client, data + off, size - off);
    if (n <= 0)
      break;
    off += n;
  }
  ::shutdown(client, SHUT_WR);
  {
    APIPlaintextFrameHelper helper(std::make_unique<socket::Socket>(server));
    if (helper.init() == APIError::OK) {
      for (int i = 0; i < 1024; i++) {
        ReadPacketBuffer buf{};
        APIError err = helper.read_packet(&buf);
        if (err == APIError::WOULD_BLOCK)
          continue;
        if (err != APIError::OK)
          break;
        dispatch(buf.type, buf.data, buf.data_len);
      }
    }
  }
  ::close(client);
  return 0;
}
