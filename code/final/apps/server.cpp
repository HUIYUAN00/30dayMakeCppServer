#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "pine.h"

namespace {
constexpr size_t kMaxLineSize = 1024 * 1024;
}  // namespace

int main(int argc, char *argv[]) {
  uint16_t port = 1234;
  int idle_timeout = 300;
  size_t max_connections = 10000;

  if (argc > 1) {
    port = static_cast<uint16_t>(std::stoi(argv[1]));
  }
  if (argc > 2) {
    idle_timeout = std::stoi(argv[2]);
  }
  if (argc > 3) {
    max_connections = static_cast<size_t>(std::stoul(argv[3]));
  }

  try {
    TcpServer server("0.0.0.0", port);
    server.setIdleTimeout(idle_timeout);
    server.setMaxConnections(max_connections);

    Signal::signal(SIGINT, [&] { server.Stop(); });
    Signal::signal(SIGTERM, [&] { server.Stop(); });

    server.onConnect([](Connection *conn) {
      std::cout << "new connection fd=" << conn->fd() << std::endl;
    });

    // 行协议：以 '\n' 为消息边界，支持拆包/粘包
    server.onRecv([](Connection *conn) {
      Buffer *buf = conn->read_buf();
      while (conn->state() == Connection::State::Connected) {
        const std::string &data = buf->buf();
        size_t pos = data.find('\n');
        if (pos == std::string::npos) {
          if (data.size() > kMaxLineSize) {
            std::cout << "message too long, close fd=" << conn->fd() << std::endl;
            conn->Close();
          }
          break;
        }

        size_t len = pos + 1;
        if (len > kMaxLineSize) {
          std::cout << "message too long, close fd=" << conn->fd() << std::endl;
          conn->Close();
          break;
        }

        std::string line = data.substr(0, len);
        buf->Retrieve(len);
        if (conn->Send(line) != RC_SUCCESS) {
          break;
        }
      }
    });

    std::cout << "server listening on port " << port << ", idle_timeout=" << idle_timeout
              << "s, max_connections=" << max_connections << std::endl;
    server.Start();
    std::cout << "server stopped" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "server error: " << e.what() << std::endl;
    return 1;
  }
}
